#include <linux/kernel.h>
#include <linux/rculist.h>
#include <linux/syscalls.h>

SYSCALL_DEFINE3(getchildids, pid_t, pid, pid_t __user *, buf,int,max){
	printk("in getchildids(%d,%d)\n",pid,max);	
	rcu_read_lock();
	struct task_struct* task = find_task_by_pid_ns(pid,&init_pid_ns);
	struct list_head* children_head = &(task->children);
	struct list_head* p;
	int i=0;
	list_for_each(p, children_head){
		struct task_struct* tsk = list_entry(p,struct task_struct,sibling);
		if(i<max){
			buf[i++] = tsk->pid;
		}
		else 
			i++;	
	}
	rcu_read_unlock();
	return i;
}

SYSCALL_DEFINE3(getthreadids, pid_t, pid, pid_t __user *, buf, int, max){
	printk("in getthreadids(): %d %d\n",pid,max);
	rcu_read_lock();
	struct task_struct* parent_task = find_task_by_pid_ns(pid,&init_pid_ns);
	struct task_struct* t = parent_task;
	int i=0;
	while((t = list_entry_rcu(t->thread_group.next,struct task_struct,thread_group))!=parent_task){
		if(i<max)
			buf[i++]= t->pid;
		else
			i++;
	}
	rcu_read_unlock();
	return i;
}
