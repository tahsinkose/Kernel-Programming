/*
 * Copyright (C) 2001 Alessandro Rubini and Jonathan Corbet
 * Copyright (C) 2001 O'Reilly & Associates
 *
 * The source code in this file can be freely used, adapted,
 * and redistributed in source or binary form, so long as an
 * acknowledgment appears in derived source files.  The citation
 * should list that the code comes from the book "Linux Device
 * Drivers" by Alessandro Rubini and Jonathan Corbet, published
 * by O'Reilly & Associates.   No warranty is attached;
 * we cannot take responsibility for errors or fitness for use.
 *
 */

#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/init.h>
#include <linux/kernel.h>	/* printk() */
#include <linux/slab.h>		/* kmalloc() */
#include <linux/fs.h>		/* everything... */
#include <linux/errno.h>	/* error codes */
#include <linux/types.h>	/* size_t */
#include <linux/proc_fs.h>
#include <linux/fcntl.h>	/* O_ACCMODE */
#include <linux/aio.h>
#include <linux/uio.h>
#include <linux/seq_file.h>
#include <linux/uaccess.h>
#include <linux/wait.h>
#include <linux/poll.h>
#include <linux/kthread.h>

#include "reminder.h"		/* local definitions */

#define  DEVICE_NAME "reminder" 

int numminors;
module_param(numminors,int,8);
MODULE_AUTHOR("Tahsincan Kose");
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Project2 for Advanced Unix Course");  ///< The description -- see modinfo
MODULE_VERSION("0.1");   

static int    majorNumber;
struct reminder_dev *reminder_devices; /* allocated in reminder_init */

// Function prototypes for file_operations
int reminder_open (struct inode *inode, struct file *filp);
static unsigned int reminder_poll(struct file *filp, poll_table *wait);
ssize_t reminder_read (struct file *filp, char __user *buf, size_t count,loff_t *f_pos);
long reminder_ioctl (struct file *filp,unsigned int cmd, unsigned long arg);
static void reminder_setup_cdev(struct reminder_dev *dev, int index);
static void reminder_exit(void);


/* Write related function prototypes */
void reminder_do_delayed_write(struct work_struct *w);
static ssize_t reminder_write (struct file *filp, const char __user *buf, size_t count, loff_t *f_pos);
static void write_extend_helper(struct reminder_dev* dev,int channel_id,size_t count);
static ssize_t reminder_delayed_write(struct file *filp,char *buf, size_t count);


/* Release related function prototypes */
int reminder_release (struct inode *inode, struct file *filp);
void do_release_writer(struct file *filp);
void do_release_reader(struct file *filp);


struct async_work {
    struct file* filp;
    char* buf;
    size_t count;
    struct delayed_work work;
};

/*
 * The fops
 */
struct file_operations reminder_fops = {
        .owner =     THIS_MODULE,
        .read =      reminder_read,
        .write =     reminder_write,
        .unlocked_ioctl =     reminder_ioctl,
        .open =      reminder_open,
        .release =   reminder_release,
        .poll = reminder_poll,
};

/*
 * Open and release
 */

int reminder_open (struct inode *inode, struct file *filp)
{
    
    struct reminder_dev *dev; /* device information */
    struct reader *reader_tmp,*reader_tmp2;
    struct writer *writer_tmp,*writer_tmp2;
    /*  Find the device */
    dev= container_of(inode->i_cdev, struct reminder_dev, cdev);
    /* and use filp->private_data to point to the device data */
    if ( (filp->f_flags & O_ACCMODE) == O_RDONLY || (filp->f_flags & O_ACCMODE) == O_RDWR)
    {
        if(down_interruptible(&dev->sem))
            return -ERESTARTSYS;
        dev->nreaders++;
        
        reader_tmp = reader_tmp2 = dev->readers;
        //printk("Before-Readers begin at %p\n",dev->readers);
        while(reader_tmp){
            reader_tmp2 = reader_tmp;
            reader_tmp = reader_tmp->next;
        }
        if(reader_tmp == reader_tmp2){// both null, hence no need to assign the next pointer
            reader_tmp = kmalloc(sizeof(struct reader),GFP_KERNEL);
            dev->readers = reader_tmp;
        }
        else{
            reader_tmp = kmalloc(sizeof(struct reader),GFP_KERNEL);
            reader_tmp2->next = reader_tmp;
        }

       /* printk("Created reader at %p\n",reader_tmp);
        printk("After-Readers begin at %p\n",dev->readers);
        printk(KERN_INFO "reminder: Created reader with pid=%d.\n",current->pid);*/
        reader_tmp->reader_pid = current->pid;
        reader_tmp->subscription_set = 1;
        init_waitqueue_head(&(reader_tmp->sub_q));
        reader_tmp->sub[0] = dev->channels[0]->wp; // Assign to the write pointer of channel.
        reader_tmp->next = NULL;
        up(&dev->sem);
    }
    if((filp->f_flags & O_ACCMODE) == O_WRONLY || (filp->f_flags & O_ACCMODE) == O_RDWR)
    {
        if(down_interruptible(&dev->sem))
            return -ERESTARTSYS;
        dev->nwriters++;
        writer_tmp = writer_tmp2 = dev->writers;
        //printk("Before->Beginning of writers at %p\n",dev->writers);
        while(writer_tmp){
            writer_tmp2 = writer_tmp;
            writer_tmp = writer_tmp->next;
            
        }
        if(writer_tmp == writer_tmp2){// both null, hence no need to assign the next pointer
            writer_tmp = kmalloc(sizeof(struct writer),GFP_KERNEL);
            dev->writers = writer_tmp;

        }
        else{
            writer_tmp = kmalloc(sizeof(struct writer),GFP_KERNEL);
            writer_tmp2->next = writer_tmp;
        }
        /*printk("Created writer at %p\n",writer_tmp);
        printk("After->Beginning of writers at %p\n",dev->writers);
        printk(KERN_INFO "reminder: Created writer with pid=%d.\n",current->pid);*/
        writer_tmp->writer_pid = current->pid;
        writer_tmp->delay_in_ms = 0; //default
        writer_tmp->next = NULL;
        up(&dev->sem);
    }
    filp->private_data = dev;
    
    return 0;
}

void do_release_writer(struct file *filp)
{
    struct reminder_dev* dev = filp->private_data;
    struct writer *tmp, *tmp2;
    tmp = tmp2 = dev->writers;
    printk("Release writer: current pid : %d\n",current->pid);
    printk("Before->Writer beginning: %p\n",dev->writers);
    while(tmp->writer_pid != current->pid){//Find the writer releasing.
        tmp2 = tmp;
        tmp = tmp->next;      
    }
    
    if(tmp == tmp2){ // In the beginning of ll
        printk("From beginning->Release writer with %p, and with next pointer %p\n",tmp,tmp->next);
        if(tmp->next) dev->writers = tmp->next;
        else dev->writers = NULL;
        kfree(tmp);
        tmp = NULL;
    }
    else{
        tmp2->next = tmp->next;
        kfree(tmp);
        tmp = NULL;
    }
    printk("After->Writer beginning: %p\n",dev->writers);
}

void do_release_reader(struct file *filp)
{
    struct reminder_dev* dev = filp->private_data;
    struct reader *tmp, *tmp2;
    int i;
    tmp = tmp2 = dev->readers;
    /*printk("Release reader: current pid : %d\n",current->pid);
    printk("Before->Reader beginning: %p\n",dev->readers);*/
    while(tmp->reader_pid != current->pid){ // Find the reader releasing.
        tmp2 = tmp;
        tmp = tmp->next;      
    }
    printk("Release reader with %p\n",tmp);
    for(i=0;i<REMINDER_CHANNELS;i++)
        tmp->sub[i] = NULL;
    if(tmp == tmp2){ // In the beginning of ll
        if(tmp->next) dev->readers = tmp->next;
        else dev->readers = NULL;
        kfree(tmp);
        tmp = NULL;
    }
    else{
        tmp2->next = tmp->next;
        kfree(tmp);
        tmp = NULL;
    }
    //printk("After->Reader beginning: %p\n",dev->readers);
}

int reminder_release (struct inode *inode, struct file *filp)
{
    struct reminder_dev* dev = filp->private_data;
    if (down_interruptible(&dev->sem))
        return -ERESTARTSYS;   
    
    
    
    if((filp->f_flags & O_ACCMODE) == O_WRONLY || (filp->f_flags & O_ACCMODE) == O_RDWR){
        do_release_writer(filp);
        dev->nwriters--;
    }
    if((filp->f_flags & O_ACCMODE) == O_RDONLY || (filp->f_flags & O_ACCMODE) == O_RDWR){
        do_release_reader(filp);
        dev->nreaders--;    
    }
    up(&dev->sem);
    printk(KERN_INFO "reminder: Device successfully closed\n");
    return 0;
}

/*
 * Init
 */
int reminder_init(void)
{
    int result, i;
    dev_t dev = MKDEV(0, 0); // Dynamic allocation ensured.
    printk(KERN_INFO "reminder: Initializing the reminder LKM\n");
    result = alloc_chrdev_region(&dev, 0, numminors, "reminder");
    majorNumber = MAJOR(dev);
    if(result < 0)
    {
        printk(KERN_ALERT "reminder failed to allocate a major number\n");
    }
    if (majorNumber<0){
      printk(KERN_ALERT "reminder failed to register a major number\n");
      return result;
    }

    printk(KERN_INFO "reminder: registered correctly with major number %d\n", majorNumber);
    /*
     * allocate the devices -- we can't have them static, as the number
     * can be specified at load time
     */
    reminder_devices = kmalloc(numminors*sizeof (struct reminder_dev), GFP_KERNEL);
    if (!reminder_devices) {
        result = -ENOMEM;
        goto fail_malloc;
    }
    memset(reminder_devices, 0, numminors*sizeof (struct reminder_dev));
    for(i = 0;i < numminors;i++){
        reminder_devices[i].nreaders = 0;
        reminder_devices[i].nwriters = 0;
        sema_init(&(reminder_devices[i].sem),1);
        reminder_setup_cdev(&(reminder_devices[i]), i);
    }
    
    printk(KERN_INFO "reminder: Cdev setup is successfull \n");

    return 0; /* success */

    fail_malloc:
        unregister_chrdev(majorNumber, DEVICE_NAME);
        return result;
}

static void reminder_setup_cdev(struct reminder_dev *dev, int index)
{
    int j;
    int err, devno = MKDEV(majorNumber, index);
    cdev_init(&dev->cdev, &reminder_fops);

    dev->cdev.owner = THIS_MODULE;
    dev->cdev.ops = &reminder_fops;
    err = cdev_add (&dev->cdev, devno, 1);
    
    for(j=0;j<REMINDER_CHANNELS;j++){
        dev->channels[j] = kmalloc(sizeof(struct channel),GFP_KERNEL);
        dev->channels[j]->data = kmalloc(REMINDER_QUANTUM,GFP_KERNEL); // Start each channel with Quantum
        dev->channels[j]->rp = dev->channels[j]->wp = dev->channels[j]->data;
        dev->channels[j]->buffer_size = REMINDER_QUANTUM;
        init_waitqueue_head(&(dev->channels[j]->inq));
        sema_init(&dev->channels[j]->read_sem,1);
    }
    /* Fail gracefully if need be */
    if (err){
        printk(KERN_NOTICE "Error %d adding reminder%d", err, index);
        return ;
    }
    else
        printk(KERN_INFO "Device %d added successfully",index);
}

struct threaded_channel_reader_data{
    struct reminder_dev* dev;
    struct reader* reader;
    struct file* filp;
    char *buffer;
    int channel_id;
    size_t max_read;
    size_t* last_read;
    /* Below fields provide the synchronization */
    int* channels_finished;
    int* finished;
};


int read_thread(void * arg)
{
    printk("[Thread reader] -> Beginning of reader thread\n");
    int i,f;
    size_t count;
    struct threaded_channel_reader_data * tcrd = (struct threaded_channel_reader_data *) arg;
    struct reminder_dev *dev = tcrd->dev;
    int channel_id = tcrd->channel_id;
    struct file* filp = tcrd->filp;
    struct reader* reader = tcrd->reader;
    size_t max_read = tcrd->max_read;
    
    if(down_interruptible(&dev->sem))
        return -ERESTARTSYS;
     printk("[Thread reader]-> After mutex acquire: Subbing channel %d and searching %ld bytes to read\n",channel_id,max_read);
    while(dev->channels[channel_id]->rp == dev->channels[channel_id]->wp)
    {
        printk("[Thread reader]-> Wait channel %d\n",channel_id,max_read);
        up(&dev->sem);
        if(filp->f_flags & O_NONBLOCK)
            return -EAGAIN;
        if(wait_event_interruptible(dev->channels[channel_id]->inq, ( (dev->channels[channel_id]->rp != dev->channels[channel_id]->wp) || (max_read == *(tcrd->last_read)) )))
            return -ERESTARTSYS;
        printk("[Thread reader] -> Channel %d has written data.\n",channel_id);
        if(down_interruptible(&(dev->channels[channel_id]->read_sem))) // First acquire the semaphore if there are other readers waiting at the channel
            return -ERESTARTSYS;
        printk("[Thread reader] -> Channel %d mutex is taken.\nWaiting for global mutex",channel_id);
        if(down_interruptible(&(dev->sem))) // Then acquire the global semaphor
            return -ERESTARTSYS;
        printk("[Thread reader] -> Global mutex is taken.\n",channel_id);
    }

    // At this point: New data is written to channel. This reader has taken the read and global semaphores
    // Get the min of these two values. Either more data is written to channel than user requested.
    // Or the written data is insufficient when compared to the desired data.
    // If they equal, then perfect match.
    printk("[Thread reader] -> Detected %ld bytes write\n",(size_t)(dev->channels[channel_id]->wp - dev->channels[channel_id]->rp));
    count = min(max_read,(size_t)(dev->channels[channel_id]->wp - dev->channels[channel_id]->rp));
    printk("[Thread reader] -> Read %d bytes\n",count);
    strncpy(tcrd->buffer + *(tcrd->last_read),dev->channels[channel_id]->rp,count);
    *(tcrd->last_read) += count;
    tcrd->channels_finished[channel_id] = count;

    f = 1; // if any of the channels are not finished, this will be 0ed.
    for(i = 0; i<REMINDER_CHANNELS;i++)
    {
        printk("[Thread reader] -> Channel %d has %d bytes written\n",channel_id,tcrd->channels_finished[channel_id]);
        if(tcrd->channels_finished[i]==0){
            f =0;
            break;
        }
    }
    if(f && count == max_read){ // Have the main read thread block until required num of bytes are read.
        *(tcrd->finished) = 1; 
        wake_up_interruptible(&reader->sub_q);
    }
    up(&dev->sem);
    up(&(dev->channels[channel_id]->read_sem));
    
    return 0;
}

ssize_t reminder_do_read (struct file *filp, char __user *buf, size_t count,
        loff_t *f_pos)
{
    int i, *finished, *channels_finished;
    struct reminder_dev *dev = filp->private_data;
    size_t tcrd_sz;
    uint16_t bitset;
    struct reader* reader;
    pid_t pid;
    size_t* last_read;
    char* buffer;
    tcrd_sz = 0;
    reader = dev->readers;
    pid = current->pid;
    while(reader->reader_pid != pid){
        reader = reader->next;
    }
    bitset = reader->subscription_set;
    for(i=0;i<REMINDER_CHANNELS;i++){
        int bit_ = (bitset & 0x8000) >> 15;
        bitset <<= 1;
        if(bit_) tcrd_sz++;
    }
    struct threaded_channel_reader_data* tcrd[tcrd_sz];
    
    printk("Init read routine summing up to %d readers.\n",dev->nreaders);

    if (down_interruptible(&dev->sem))
        return -ERESTARTSYS;
    
    reader = dev->readers;
    pid = current->pid;
    while(reader->reader_pid != pid){
        reader = reader->next;
    }
    for(i=0;i<tcrd_sz;i++)
        tcrd[i] = kmalloc(sizeof(struct threaded_channel_reader_data),GFP_KERNEL);
    finished = kmalloc(sizeof(int),GFP_KERNEL);
    channels_finished = kmalloc(sizeof(int) * REMINDER_CHANNELS,GFP_KERNEL);
    buffer = kmalloc(sizeof(char)*count,GFP_KERNEL);
    bitset = reader->subscription_set;
    for(i = 0;i<REMINDER_CHANNELS;i++)
    {
        int bit_ = (bitset & 0x8000) >> 15;
        bitset <<= 1;
        channels_finished[i] = 1-bit_;
    }
    count = (count < REMINDER_QUANTUM - 1 ? count : REMINDER_QUANTUM - 1);
    bitset = reader->subscription_set;
    printk("Desired read size: %d\n",count);

    last_read = kmalloc(sizeof(size_t),GFP_KERNEL);
    *last_read = 0;
    *finished = 0;
    for(i=0;i<REMINDER_CHANNELS;i++){
        int bit_ = (bitset & 0x8000) >> 15;
        bitset <<= 1;
        if(bit_){
            
            tcrd[i]->dev = dev;
            tcrd[i]->reader = reader;
            tcrd[i]->filp = filp;
            tcrd[i]->channel_id = i;
            tcrd[i]->finished = finished;
            tcrd[i]->channels_finished = channels_finished;
            tcrd[i]->max_read = count;
            tcrd[i]->last_read = last_read;
            tcrd[i]->buffer = buffer;
            printk("[Main reader] -> Create thread for channel %d\n",i);
            kthread_run(read_thread,(void*) tcrd[i],"read->[user,channel]=%d-%d",reader->reader_pid,i);
            printk("[Main reader] -> Created thread for channel %d\n",i);
        }
    }
    up(&dev->sem);
    printk("[Main reader] Waiting for finish\n");
    wait_event_interruptible(reader->sub_q,(*finished != 0));
    if (down_interruptible(&dev->sem))
        return -ERESTARTSYS;
    if(copy_to_user(buf,buffer,count)){
        up(&dev->sem);
        return -EFAULT;
    }
    for(i = 0;i<REMINDER_CHANNELS;i++)
    {
        if(channels_finished[i]>1)
            dev->channels[i]->rp += channels_finished[i];
    }
    
    up(&dev->sem);
    kfree(tcrd);
    kfree(channels_finished);
    kfree(last_read);
    kfree(buffer);
    kfree(finished);
    return count;
}

ssize_t reminder_read (struct file *filp, char __user *buf, size_t count,
        loff_t *f_pos)
{
    return reminder_do_read(filp, buf, count, f_pos);
}

static void write_extend_helper(struct reminder_dev* dev,int channel_id,size_t count)
{
    size_t new_size,old_write,old_read,orsp_sz;
    unsigned long *old_reader_subscription_points;
    struct reader* reader;
    char *new_data;
    int i;
    new_size = dev->channels[channel_id]->wp - dev->channels[channel_id]->data + count + 1; //safety byte
    old_write = dev->channels[channel_id]->wp - dev->channels[channel_id]->data;
    old_read = dev->channels[channel_id]->rp - dev->channels[channel_id]->data;
    orsp_sz = 0;

    reader = dev->readers;
    while(reader){
        uint16_t bitset = reader->subscription_set;
        int bit_ = (bitset & 0x8000) >> (15 - (channel_id+1));
        if(bit_){
            orsp_sz++;
            old_reader_subscription_points = krealloc(old_reader_subscription_points,sizeof(unsigned long)*orsp_sz,GFP_KERNEL);
            old_reader_subscription_points[orsp_sz-1] = reader->sub[channel_id] - dev->channels[channel_id]->data; //old point
        }
        reader = reader->next; 
    }

    if(dev->channels[channel_id]->wp > dev->channels[channel_id]->data + dev->channels[channel_id]->buffer_size - count){ // Needs extension
        printk("Reallocate memory for channel %d. New size: %zd\n",channel_id,new_size);
        new_data = krealloc(dev->channels[channel_id]->data,new_size,GFP_KERNEL);
        if(!new_data){
            return ;
        }
        dev->channels[channel_id]->data = new_data;
        dev->channels[channel_id]->wp = dev->channels[channel_id]->data + old_write;
        reader = dev->readers;
        i = 0;
        while(reader){
            uint16_t bitset = reader->subscription_set;
            int bit_ = (bitset & 0x8000) >> (15 - (channel_id+1));
            if(bit_){
                reader->sub[channel_id] = dev->channels[channel_id]->data + old_reader_subscription_points[i++]; //old point
            }
            reader = reader->next; 
        }
        dev->channels[channel_id]->rp = dev->channels[channel_id]->data + old_read;
        printk("Reallocation done for channel %d. New size: %zd\n",channel_id,new_size);
        dev->channels[channel_id]->buffer_size = new_size;
    }
    kfree(old_reader_subscription_points);
}

static ssize_t reminder_do_write (struct file *filp, const char  __user *buf, size_t count,
        loff_t *f_pos)
{

    struct reminder_dev *dev = filp->private_data;
    int channel_id;
    uint8_t channel_buf[1];
    
    ssize_t retval = -ENOMEM; /* our most likely error */
    printk("Write routine summing up to %d writers.\n",dev->nwriters);

    if (down_interruptible(&dev->sem))
        return -ERESTARTSYS;
    
    if(copy_from_user(channel_buf,buf,1)){
        printk("Failed to get channel value");
        retval = -EFAULT;
        goto nomem;
    }
    channel_id = (int)channel_buf[0];
    channel_id = channel_id & 0xF;
    count--;
    count = (count < REMINDER_QUANTUM ? count : REMINDER_QUANTUM); // Truncate if necessary.

    write_extend_helper(dev,channel_id,count);
    
    if (copy_from_user (dev->channels[channel_id]->wp, buf+1, count)) {
            retval = -EFAULT;
            goto nomem;
    }
    else
        printk("[Write] -> Successfull write\n");  

    dev->channels[channel_id]->wp += count;
    printk("Write pointer is at %ld for channel %d\n",dev->channels[channel_id]->wp - dev->channels[channel_id]->data,channel_id);
    up(&dev->sem);

    wake_up_interruptible(&(dev->channels[channel_id]->inq));
    return count;

    nomem:
        up(&dev->sem);
        return retval;
}

void reminder_do_delayed_write(struct work_struct *w)
{
    int channel_id,count;
    
    struct async_work* write_to_do = container_of(w,struct async_work,work.work);

    struct reminder_dev *dev = write_to_do->filp->private_data;

    printk("Execute delayed write\n");
    printk("[Delayed Write] -> Try to take mutex\n");
    if (down_interruptible(&dev->sem)){
        printk("[Delayed Write] -> Could not take mutex\n");
        return;
    }

    channel_id = write_to_do->buf[0];
    channel_id &= 0xF;
    count = write_to_do->count;
    count--;
    count = (count < REMINDER_QUANTUM ? count : REMINDER_QUANTUM); // Truncate if necessary.

    write_extend_helper(dev,channel_id,count);

    strncpy(dev->channels[channel_id]->wp, write_to_do->buf+1, count);
    
    printk("[Delayed Write] -> Successfull write\n"); 
    dev->channels[channel_id]->wp += count;
    printk("[Delayed Write] -> Write Pointer is at %ld for channel %d\n",dev->channels[channel_id]->wp - dev->channels[channel_id]->data,channel_id);
    wake_up_interruptible(&dev->channels[channel_id]->inq);
    
    up(&dev->sem);
        
}

static ssize_t reminder_delayed_write(struct file *filp,char *buf, size_t count)
{
    struct async_work* work_to_do;
    struct writer *tmp;
    int n;
    pid_t pid;
    struct reminder_dev* dev = filp->private_data;
    if (down_interruptible(&dev->sem)){
        printk("Not able to take mutex\n");
        up(&dev->sem);
        return -ERESTARTSYS;
    }

    pid = current->pid;
    work_to_do = kmalloc (sizeof (struct async_work), GFP_KERNEL);
    tmp = dev->writers;
    while(tmp->writer_pid != pid){
        tmp = tmp->next;
    }
    n = tmp->delay_in_ms;
    work_to_do->filp = filp;
    work_to_do->buf = buf;
    work_to_do->count = count;
    INIT_DELAYED_WORK(&work_to_do->work, reminder_do_delayed_write);
    printk("Delaying write with %d ms\n",n);
    up(&dev->sem);
    schedule_delayed_work(&work_to_do->work,n*HZ/1000); //n milliseconds
    
        
    return count;
}
static ssize_t reminder_write (struct file *filp, const char __user *buf, size_t count, loff_t *offset)
{
    pid_t pid;
    struct writer* tmp;
    struct reminder_dev* dev;
    char* local_buf = kmalloc (count * sizeof (char),GFP_KERNEL);
    pid = current->pid;
    dev = filp->private_data;

    printk("Init write\n");
    if(copy_from_user(local_buf,buf,count)){
        return -EFAULT;
    }
    
    tmp = dev->writers;
    while(tmp->writer_pid != pid){
        tmp = tmp->next;
    }
    if(tmp->delay_in_ms)
        return reminder_delayed_write(filp,local_buf,count);
    else
        return reminder_do_write(filp,buf,count,offset);
}

/*
 * The ioctl() implementation
 */

long reminder_ioctl (struct file *filp,
                   unsigned int cmd, unsigned long arg)
{
    int err = 0, ret = 0;
    pid_t pid;
    struct reminder_dev *dev = filp->private_data;

    /* don't even decode wrong cmds: better returning  ENOTTY than EFAULT */
    if (_IOC_TYPE(cmd) != REMIND_IOC_MAGIC) return -ENOTTY;
    if (_IOC_NR(cmd) > REMIND_IOC_MAXNR) return -ENOTTY;
    /*
     * the type is a bitmask, and VERIFY_WRITE catches R/W
     * transfers. Note that the type is user-oriented, while
     * verify_area is kernel-oriented, so the concept of "read" and
     * "write" is reversed
     */
    if (_IOC_DIR(cmd) & _IOC_READ)
        err = !access_ok(VERIFY_WRITE, (void __user *)arg, _IOC_SIZE(cmd));
    else if (_IOC_DIR(cmd) & _IOC_WRITE)
        err =  !access_ok(VERIFY_READ, (void __user *)arg, _IOC_SIZE(cmd));
    if (err)
        return -EFAULT;
    pid = current->pid;
    
    switch(cmd) {

        case REMIND_IOCTSETSUBS:
        {
            int bitset;
            int i;
            if((filp->f_flags & O_ACCMODE) == O_WRONLY)
                return -EACCES;
            ret = __get_user(bitset, (int __user *) arg); // on success returns 0
            if(!ret){
                struct reader* tmp = dev->readers;
                while(tmp->reader_pid != pid){
                    tmp = tmp->next;
                }
                uint16_t bitset_ = bitset;
                tmp->subscription_set = bitset;
                printk("[SETSUBS] -> Reader[%d] has bitset %d\n",tmp->reader_pid,tmp->subscription_set);
                for(i=0;i<16;i++){
                    int bit_ = (bitset & 0x8000) >> 15;
                    bitset <<= 1;
                    if(bit_)
                        tmp->sub[i] = dev->channels[i]->wp; // assign read pointer to the beginning of write pointer of the specific channel.
                }
            }
            break;
        }
        case REMIND_IOCTGETSUBS:
        {
            if((filp->f_flags & O_ACCMODE) == O_WRONLY)
                return -EACCES; 
            struct reader* tmp = dev->readers;
            while(tmp->reader_pid != pid){
                tmp = tmp->next;
            }
            printk("[GETSUBS] -> Reader[%d] has bitset %d\n",tmp->reader_pid,tmp->subscription_set);
            ret = __put_user(tmp->subscription_set, (int __user *)arg);
            break;
        }
        case REMIND_IOCTSETDELAY:
        {
            if((filp->f_flags & O_ACCMODE) == O_RDONLY)
                return -EACCES;  
            int delay;
            ret = __get_user(delay, (int __user *) arg);
            printk("Delay is %d\n",delay);
            if(!ret){
                if(delay<0) return -EINVAL;
                struct writer* tmp = dev->writers;
                while(tmp->writer_pid != pid){
                    tmp = tmp->next;
                }
                tmp->delay_in_ms = delay;
            }
            break;
        }
        case REMIND_IOCQGETDELAY:
        {  
            if((filp->f_flags & O_ACCMODE) == O_RDONLY)
                return -EACCES;
            struct writer* tmp = dev->writers;
            while(tmp->writer_pid != pid){
                tmp = tmp->next;
            }
            ret = __put_user(tmp->delay_in_ms, (int __user *)arg);
            break;
        }
        case REMIND_IOCQGETPENDING:
            break;
        default:
            return -ENOTTY;
    }

    return ret;
}


int reminder_trim(struct reminder_dev* dev)
{
    int i;
    struct reminder_dev *next, *dptr;
    if(dev->vmas)
        return -EBUSY;

    for(dptr = dev; dptr; dptr = next){ // All minor devices
        for(i = 0;i<REMINDER_CHANNELS;i++){
            if(dptr->channels[i]->data){
                kfree(dptr->channels[i]->data);
                dptr->channels[i]->data = NULL;

            }
            dptr->channels[i]->buffer_size = 0;
            dptr->channels[i]->rp =NULL;
            dptr->channels[i]->wp = NULL;
        }
        // Force remove any reader and writer.
        if(dptr->readers)
            kfree(dptr->readers);
        if(dptr->writers)
            kfree(dptr->writers);
        next = dptr->next;
        if(dptr != dev) kfree(dptr);
    }
    dev->next = NULL;
    return 0;
}

static unsigned int reminder_poll(struct file *filp, poll_table *wait)
{
    int i;
    struct reader* reader;
    uint16_t bitset;
    pid_t pid;
    struct reminder_dev * dev = filp->private_data;
    unsigned int mask = 0;
    if (down_interruptible(&dev->sem))
        return -ERESTARTSYS;
    reader = dev->readers;
    pid = current->pid;
    while(reader->reader_pid != pid){
        reader = reader->next;
    }
    bitset = reader->subscription_set;
    for(i=0;i<REMINDER_CHANNELS;i++){
        int bit_ = (bitset & 0x8000) >> 15;
        bitset <<= 1;
        if(bit_) poll_wait(filp,&dev->channels[i]->inq,wait); 
    }
    bitset = reader->subscription_set;
    for(i=0;i<REMINDER_CHANNELS;i++){
        int bit_ = (bitset & 0x8000) >> 15;
        bitset <<= 1;
        if(bit_){
            if(dev->channels[i]->rp != dev->channels[i]->wp)
                mask |= POLLIN | POLLRDNORM;
        }
    }
    up(&dev->sem);
    return mask;
}

static void reminder_exit(void)
{

    int i;
    for(i = 0;i < numminors;i++){
        cdev_del(&(reminder_devices[i].cdev));
        reminder_trim(reminder_devices + i);    
    }
    
    kfree(reminder_devices);
    
    unregister_chrdev_region(MKDEV(majorNumber,0), numminors);
}




module_init(reminder_init);
module_exit(reminder_exit);
