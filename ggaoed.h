#ifndef GGAOED_H
#define GGAOED_H

#ifdef HAVE_SYS_EVENTFD_H
#include <sys/eventfd.h>
#endif
#include <sys/epoll.h>
#include <libaio.h>
#include <syslog.h>

#include <glib.h>

#include "aoe.h"
#include "ctl.h"

/* glibc 2.7 have eventfd support but does not ship <sys/eventfd.h> */
#ifndef HAVE_SYS_EVENTFD_H
typedef uint64_t eventfd_t;
int eventfd(unsigned initval, int flags);
#endif /* HAVE_SYS_EVENTFD_H */

/**********************************************************************
 * Constants
 */

typedef enum
{
	EMPTY = 0,
	PENDING,		/* I/O is prepared but not yet submitted */
	SUBMITTED,		/* Disk I/O has been submitted */
	READY,			/* The result is ready to be sent */
	FLUSH			/* Cache flush request */
} queue_state;

#define SHELF_BCAST		0xffff
#define SLOT_BCAST		0xff

#define MIN_QUEUE_LEN		4
#define MAX_QUEUE_LEN		1024
#define DEF_QUEUE_LEN		64

#define MAX_BUFFERS		8192
#define DEF_BUFFERS		4

#define MAX_LBA28		0x0fffffffLL
#define MAX_LBA48		0x0000ffffffffffffLL

/**********************************************************************
 * Data types
 */

/* I/O event handler callback prototype */
typedef void (*io_callback)(uint32_t events, void *data);

/* Configuration defaults */
struct default_config
{
	int			queue_length;
	int			direct_io;
	int			trace_io;
	GPtrArray		*interfaces;
	GPtrArray		*acls;
	int			mtu;
	int			buffers;
	char			*pid_file;
	char			*ctl_socket;
};

/* Device configuration */
struct device_config
{
	char			*path;
	int			shelf;
	int			slot;
	int			queue_length;
	int			direct_io;
	int			trace_io;
	int			read_only;
	int			broadcast;

	/* Patterns of allowed interfaces */
	GPtrArray		*iface_patterns;

	/* ACLs */
	GArray			*accept;
	GArray			*deny;
};

/* Network interface configuration */
struct netif_config
{
	int			mtu;
	int			buffers;
};

/* Event handler context */
struct event_ctx
{
	io_callback		callback;
};

/* Elements of a device's I/O queue */
struct device;
struct queue_item
{
	struct iocb		iocb;
	struct netif		*iface;
	struct device		*dev;
	struct timespec		start;

	void			*buf;
	unsigned		bufsize;
	unsigned		length;
	unsigned		dynalloc;

	union
	{
		struct aoe_hdr		aoe_hdr;
		struct aoe_ata_hdr	ata_hdr;
		struct aoe_cfg_hdr	cfg_hdr;
	};
	unsigned		hdrlen;

	queue_state		state;
};

/* State of an exported device */
struct device
{
	/* This must be the first element of the struct */
	struct event_ctx	event_ctx;
	int			event_fd;

	int			fd;

	int			congested:1;
	int			io_stall:1;

	char			*name;
	unsigned long long	size;
	struct device_config	cfg;
	struct device_stats	stats;

	void			*aoe_conf;
	unsigned		aoe_conf_len;

	unsigned		q_mask;
	unsigned		q_head;
	unsigned		q_tail;

	io_context_t		aio_ctx;
	unsigned		submitted;

	struct queue_item	**queue;

	/* List of attached interfaces */
	GPtrArray		*ifaces;
};

/* ACL definition */
struct acl
{
	char			*name;
	GArray			*entries;
};

/* State of a network interface */
struct netif
{
	/* This must be the first element of the struct */
	struct event_ctx	event_ctx;

	struct netif_config	cfg;
	struct netif_stats	stats;

	char			*name;
	int			ifindex;
	int			mtu;
	int			fd;

	int			congested:1;

	struct ether_addr	mac;

	void			*ringptr;
	unsigned		ringlen;
	void			**ring;
	unsigned		ringcnt;
	unsigned		ringidx;
	unsigned		frame_size;
	unsigned		tp_hdrlen;

	/* Devices that can be accessed on this interface */
	GPtrArray		*devices;
};

/**********************************************************************
 * Prototypes
 */

void validate_iface(const char *name, int ifindex, int mtu, const char *macaddr);
void invalidate_iface(int ifindex);
void setup_ifaces(void);
void done_ifaces(void);
void report_net_stats(int fd);

void logit(int level, const char *fmt, ...) G_GNUC_PRINTF(2, 3);
#define logerr(fmt, ...) \
	logit(LOG_ERR, fmt ": %s", ##__VA_ARGS__, strerror(errno))
#define devlog(dev, level, fmt, ...) \
	logit(level, "disk/%s: " fmt, dev->name, ##__VA_ARGS__)
#define deverr(dev, fmt, ...) \
	devlog(dev, LOG_ERR, fmt ": %s", ##__VA_ARGS__, strerror(errno))
#define netlog(iface, level, fmt, ...) \
	logit(level, "net/%s: " fmt, iface->name, ##__VA_ARGS__)
#define neterr(iface, fmt, ...) \
	netlog(iface, LOG_ERR, fmt ": %s", ##__VA_ARGS__, strerror(errno))

void *alloc_packet(unsigned size);
void free_packet(void *buf, unsigned size);
void mem_init(void);
void mem_done(void);

void netmon_open(void);
void netmon_enumerate(void);
void netmon_close(void);

void add_fd(int fd, struct event_ctx *ctx);
void del_fd(int fd);
void modify_fd(int fd, struct event_ctx *ctx, uint32_t events);

void process_request(struct netif *iface, struct device *device, void *buf, int len, struct timespec *tv);
void attach_devices(struct netif *iface);
void detach_device(struct netif *iface, struct device *device);
void setup_devices(void);
void done_devices(void);
void run_queue(struct device *dev, int sync);
void report_dev_stats(int fd);

int match_patternlist(GPtrArray *list, const char *str);
int get_device_config(const char *name, struct device_config *devcfg);
void destroy_device_config(struct device_config *devcfg);
int get_netif_config(const char *name, struct netif_config *netcfg);
void resolve_acls(GArray **acls, char **list, const char *msgprefix);
unsigned long long human_format(unsigned long long size, const char **unit);

void ctl_init(void);
void ctl_done(void);

/**********************************************************************
 * Global variables
 */

extern GKeyFile *global_config;
extern volatile int exit_flag;
extern volatile int reload_flag;
extern struct default_config defaults;

#endif /* GGAOED_H */
