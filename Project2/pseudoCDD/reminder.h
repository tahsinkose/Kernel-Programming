/* -*- C -*-
 * scullc.h -- definitions for the scullc char module
 *
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
 */


/*
	This source code originates form the book "Linux Device Drivers" by Allensandro Rubini and Jonathan
	Corbet, published by O'Reilly & Associates.
*/

#include <linux/ioctl.h>
#include <linux/cdev.h>
#include <linux/wait.h>
#define REMINDER_QUANTUM  4096 /* use a quantum size like scull */
#define REMINDER_CHANNELS     16

struct reader{
	uint16_t subscription_set;
	pid_t reader_pid;
	char* sub[REMINDER_CHANNELS];
	wait_queue_head_t sub_q;
	struct reader* next;
};
struct writer{
	int delay_in_ms;
	pid_t writer_pid;
	struct writer* next;
};

struct channel{
	char* data;
	char* rp;
	char* wp;
	size_t buffer_size;
	struct semaphore read_sem;
	wait_queue_head_t inq;
};
// Implement with channel version
struct reminder_dev {
	struct channel* channels[REMINDER_CHANNELS];
	struct reminder_dev *next;  /* next listitem */
	int vmas;                 /* active mappings */
	int nreaders,nwriters;
	struct reader* readers;
	struct writer* writers;
	struct semaphore sem;     /* Mutual exclusion */
	struct cdev cdev;
};

#define REMIND_IOC_MAGIC  'd'

#define REMIND_IOCTSETSUBS _IO(REMIND_IOC_MAGIC, 1)
#define REMIND_IOCTGETSUBS _IO(REMIND_IOC_MAGIC, 2)
#define REMIND_IOCTSETDELAY _IO(REMIND_IOC_MAGIC, 3)
#define REMIND_IOCQGETDELAY _IO(REMIND_IOC_MAGIC, 4)
#define REMIND_IOCQGETPENDING _IO(REMIND_IOC_MAGIC, 5)
#define REMIND_IOC_MAXNR 5

