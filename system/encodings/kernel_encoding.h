//
// Created by Giuliano Gambacorta on 29/05/19.
//
#ifndef KERNEL_ENCODING_H
#define KERNEL_ENCODING_H

static int encodings_init(void);
static void encodings_exit(void);
static int     dev_open(struct inode *, struct file *);
static int     dev_release(struct inode *, struct file *);
static ssize_t dev_read(struct file *, char *, size_t, loff_t *);
static ssize_t dev_write(struct file *, const char *, size_t, loff_t *);
static int encode(char *, const unsigned *, const unsigned *);

#endif //KERNEL_ENCODING_H
