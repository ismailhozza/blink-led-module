#include <linux/init.h>
#include <linux/module.h>
#include <linux/gpio.h>
#include <linux/slab.h>
#include <linux/cdev.h>
#include <linux/kernel.h>
#include <linux/fs.h>
#include <linux/sysfs.h>
#include <linux/device.h>
#include <linux/delay.h>

#define LED_GPIO 26

MODULE_LICENSE("GPL");

#define DEVICE_NAME "led02"
#define CLASS_NAME "ledclass"

long blink_delay = 50;

struct led_dev {
    struct cdev cdev;
};

struct file_operations fops = {
    .owner = THIS_MODULE
};

int led_on = 0;
int blinking_on = 0;

static int init_led_module(void);
static void exit_led_module(void);
static ssize_t led_store(struct device *dev, struct device_attribute *attr, const char *buf, size_t count);
static ssize_t led_show(struct device *dev, struct device_attribute *attr, char *buf);
void led_blink_function(struct work_struct *work);


dev_t led_dev_number;
struct device *led_device;
struct class *led_class;
struct led_dev *my_dev;

static DEVICE_ATTR(led_attr, S_IRUSR | S_IWUSR, led_show, led_store);

static struct workqueue_struct *my_wq;

struct my_delayed_work {
    struct delayed_work  delayed_work;
};

struct my_delayed_work *delayed_work;

static __init int init_led_module(void) {
    printk(KERN_DEBUG "Init led module\n");

    my_wq = alloc_workqueue("my_wq", 0, 4);
    if(my_wq) {
        delayed_work = (struct my_delayed_work *)kzalloc(sizeof(struct my_delayed_work), GFP_KERNEL);
        if(delayed_work) {
            INIT_DELAYED_WORK((struct delayed_work *)delayed_work, &led_blink_function);
        } else {
            goto work;
        }
    } else {
        return -ENODEV;
    }

    if(alloc_chrdev_region(&led_dev_number, 0, 1, DEVICE_NAME) < 0) {
        printk(KERN_ALERT "Can't reserve dev_t\n");
        return -ENODEV;
    }
    my_dev = (struct led_dev *)kmalloc(sizeof(struct led_dev), GFP_KERNEL);
    if(my_dev == NULL) {
        printk(KERN_ALERT "Can't reserve memory\n");
        goto chrdev;
    }
    led_class = class_create(THIS_MODULE, CLASS_NAME);
    led_device = device_create(led_class, NULL, led_dev_number, NULL, DEVICE_NAME);
    if(device_create_file(led_device, &dev_attr_led_attr) < 0) {
        printk(KERN_ALERT "Can't create device file for sysfs\n");
        goto class;
    }
    cdev_init(&my_dev->cdev, &fops);
    if(cdev_add(&my_dev->cdev, led_dev_number, 1) < 0 ) {
        printk(KERN_ALERT "Can't add cdev");
        goto class;
    }
    if(!gpio_is_valid(LED_GPIO)) {
        printk(KERN_ALERT "Chosen gpio %d is not valid\n", LED_GPIO);
        goto cdev;
    }
    gpio_request(LED_GPIO, DEVICE_NAME);
    gpio_direction_output(LED_GPIO, led_on);
    return 0;

cdev:
    cdev_del(&my_dev->cdev);

class:
    device_destroy(led_class, led_dev_number);
    class_destroy(led_class);
    kfree(my_dev);

chrdev:
    kfree(delayed_work);
    unregister_chrdev_region(led_dev_number, 1);

work:
    flush_workqueue(my_wq);
    destroy_workqueue(my_wq);
    return -ENODEV;
}

static void exit_led_module(void) {
    printk(KERN_DEBUG "Exit led module\n");
    gpio_set_value(LED_GPIO, 0);
    gpio_free(LED_GPIO);
    cdev_del(&my_dev->cdev);
    device_destroy(led_class, led_dev_number);
    class_destroy(led_class);
    kfree(my_dev);
    unregister_chrdev_region(led_dev_number, 1);
    flush_workqueue(my_wq);
    destroy_workqueue(my_wq);
    kfree(delayed_work);
}

static ssize_t led_store(struct device *dev, struct device_attribute *attr, const char *buf, size_t count) {
    printk(KERN_DEBUG "Led store called, buf %s\n", buf);
    if(blinking_on == 0 && strncmp(buf, "on", 2) == 0) {
        blinking_on = 1;
        queue_delayed_work(my_wq, (struct delayed_work *)delayed_work, blink_delay);
        printk(KERN_DEBUG "Queued new blinking work at %ld\n", jiffies);
    } else if (blinking_on == 1 && strncmp(buf, "off", 3) == 0) {
        blinking_on = 0;
        printk(KERN_DEBUG "Stopped blinking\n");
    } else {
        long res = -1;
        if(kstrtol(buf, 10, &res) == 0 && res >= 50 && res <= 1000) {
            printk(KERN_DEBUG "Setting a new delay interval to: %ld\n", res);
            blink_delay = res/10;
        } else {
            printk(KERN_DEBUG "Invalid command given. Allowed commands are: 'on', 'off' or number ([50,1000])\n");
        }
    }

    return count;
}

static ssize_t led_show(struct device *dev, struct device_attribute *att, char *buf) {
    printk("Led show called\n");

    return sprintf(buf, "%ld milliseconds\n", blink_delay*10);
}

void led_blink_function(struct work_struct *work) {
    printk(KERN_DEBUG "Led led_blink_function called\n");
    if(led_on == 0 ) {
        led_on = 1;
    } else {
        led_on = 0;
    }
    if(blinking_on == 1) {
        // mdelay(500);
        queue_delayed_work(my_wq, (struct delayed_work *)work, blink_delay);
        printk(KERN_DEBUG "Re-queued work at %ld\n", jiffies);
    } else {
        gpio_set_value(LED_GPIO, 0);
        return;
    }
    gpio_set_value(LED_GPIO, led_on);
}

module_init(init_led_module);
module_exit(exit_led_module);