/* SPDX-License-Identifier: LGPL-2.1+ */

#define _GNU_SOURCE
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <getopt.h>
#include <libgen.h>
#include <limits.h>
#include <linux/bpf.h>
#include <linux/sched.h>
#include <linux/seccomp.h>
#include <sched.h>
#include <signal.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/mount.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#ifndef MOUNT_ATTR_RDONLY
#define MOUNT_ATTR_RDONLY 0x00000001
#endif

#ifndef MOUNT_ATTR_NOSUID
#define MOUNT_ATTR_NOSUID 0x00000002
#endif

#ifndef MOUNT_ATTR_NOEXEC
#define MOUNT_ATTR_NOEXEC 0x00000008
#endif

#ifndef MOUNT_ATTR_NODIRATIME
#define MOUNT_ATTR_NODIRATIME 0x00000080
#endif

#ifndef MOUNT_ATTR__ATIME
#define MOUNT_ATTR__ATIME 0x00000070
#endif

#ifndef MOUNT_ATTR_RELATIME
#define MOUNT_ATTR_RELATIME 0x00000000
#endif

#ifndef MOUNT_ATTR_NOATIME
#define MOUNT_ATTR_NOATIME 0x00000010
#endif

#ifndef MOUNT_ATTR_STRICTATIME
#define MOUNT_ATTR_STRICTATIME 0x00000020
#endif

#ifndef MOUNT_ATTR_SHIFT
#define MOUNT_ATTR_SHIFT 0x00100000
#endif

#ifndef AT_RECURSIVE
#define AT_RECURSIVE 0x8000
#endif

#ifndef MAKE_PROPAGATION_SHARED
#define MAKE_PROPAGATION_SHARED 4
#endif

#ifndef OPEN_TREE_CLONE
#define OPEN_TREE_CLONE 1
#endif

#ifndef OPEN_TREE_CLOEXEC
#define OPEN_TREE_CLOEXEC O_CLOEXEC
#endif

/* move_mount() flags */
#ifndef MOVE_MOUNT_F_SYMLINKS
#define MOVE_MOUNT_F_SYMLINKS 0x00000001 /* Follow symlinks on from path */
#endif

#ifndef MOVE_MOUNT_F_AUTOMOUNTS
#define MOVE_MOUNT_F_AUTOMOUNTS 0x00000002 /* Follow automounts on from path */
#endif

#ifndef MOVE_MOUNT_F_EMPTY_PATH
#define MOVE_MOUNT_F_EMPTY_PATH 0x00000004 /* Empty from path permitted */
#endif

#ifndef MOVE_MOUNT_T_SYMLINKS
#define MOVE_MOUNT_T_SYMLINKS 0x00000010 /* Follow symlinks on to path */
#endif

#ifndef MOVE_MOUNT_T_AUTOMOUNTS
#define MOVE_MOUNT_T_AUTOMOUNTS 0x00000020 /* Follow automounts on to path */
#endif

#ifndef MOVE_MOUNT_T_EMPTY_PATH
#define MOVE_MOUNT_T_EMPTY_PATH 0x00000040 /* Empty to path permitted */
#endif

#ifndef MOVE_MOUNT__MASK
#define MOVE_MOUNT__MASK 0x00000077
#endif

#ifndef __NR_mount_setattr
	#if defined __alpha__
		#define __NR_mount_setattr 551
	#elif defined _MIPS_SIM
		#if _MIPS_SIM == _MIPS_SIM_ABI32	/* o32 */
			#define __NR_mount_setattr 4441
		#endif
		#if _MIPS_SIM == _MIPS_SIM_NABI32	/* n32 */
			#define __NR_mount_setattr 6441
		#endif
		#if _MIPS_SIM == _MIPS_SIM_ABI64	/* n64 */
			#define __NR_mount_setattr 5441
		#endif
	#elif defined __ia64__
		#define __NR_mount_setattr (441 + 1024)
	#else
		#define __NR_mount_setattr 441
	#endif
#endif

#ifndef __NR_fsconfig
	#if defined __alpha__
		#define __NR_fsconfig 541
	#elif defined _MIPS_SIM
		#if _MIPS_SIM == _MIPS_SIM_ABI32	/* o32 */
			#define __NR_fsconfig 4431
		#endif
		#if _MIPS_SIM == _MIPS_SIM_NABI32	/* n32 */
			#define __NR_fsconfig 6431
		#endif
		#if _MIPS_SIM == _MIPS_SIM_ABI64	/* n64 */
			#define __NR_fsconfig 5431
		#endif
	#elif defined __ia64__
		#define __NR_fsconfig (431 + 1024)
	#else
		#define __NR_fsconfig 431
	#endif
#endif

#ifndef __NR_open_tree
	#if defined __alpha__
		#define __NR_open_tree 538
	#elif defined _MIPS_SIM
		#if _MIPS_SIM == _MIPS_SIM_ABI32	/* o32 */
			#define __NR_open_tree 4428
		#endif
		#if _MIPS_SIM == _MIPS_SIM_NABI32	/* n32 */
			#define __NR_open_tree 6428
		#endif
		#if _MIPS_SIM == _MIPS_SIM_ABI64	/* n64 */
			#define __NR_open_tree 5428
		#endif
	#elif defined __ia64__
		#define __NR_open_tree (428 + 1024)
	#else
		#define __NR_open_tree 428
	#endif
#endif

#ifndef __NR_move_mount
	#if defined __alpha__
		#define __NR_move_mount 539
	#elif defined _MIPS_SIM
		#if _MIPS_SIM == _MIPS_SIM_ABI32	/* o32 */
			#define __NR_move_mount 4429
		#endif
		#if _MIPS_SIM == _MIPS_SIM_NABI32	/* n32 */
			#define __NR_move_mount 6429
		#endif
		#if _MIPS_SIM == _MIPS_SIM_ABI64	/* n64 */
			#define __NR_move_mount 5429
		#endif
	#elif defined __ia64__
		#define __NR_move_mount (428 + 1024)
	#else
		#define __NR_move_mount 429
	#endif
#endif

#define IDMAPLEN 4096
#define STRLITERALLEN(x) (sizeof(""x"") - 1)
#define INTTYPE_TO_STRLEN(type)             \
	(2 + (sizeof(type) <= 1             \
		  ? 3                       \
		  : sizeof(type) <= 2       \
			? 5                 \
			: sizeof(type) <= 4 \
			      ? 10          \
			      : sizeof(type) <= 8 ? 20 : sizeof(int[-2 * (sizeof(type) > 8)])))
#define log_error_errno(__ret__, __errno__, format, ...)      \
	({                                                    \
		typeof(__ret__) __internal_ret__ = (__ret__); \
		errno = (__errno__);                          \
		fprintf(stderr, format, ##__VA_ARGS__);       \
		__internal_ret__;                             \
	})

struct mount_attr {
	__u64 attr_set;
	__u64 attr_clr;
	__u64 propagation;
	__u32 userns;
	__u32 reserved[0];
};

static inline int sys_mount_setattr(int dfd, const char *path, unsigned int flags,
				    struct mount_attr *attr, size_t size)
{
	return syscall(__NR_mount_setattr, dfd, path, flags, attr, size);
}

static inline int sys_fsconfig(int fd, unsigned int cmd, const char *key, const void *value, int aux)
{
	return syscall(__NR_fsconfig, fd, cmd, key, value, aux);
}

static inline int sys_open_tree(int dfd, const char *filename, unsigned int flags)
{
	return syscall(__NR_open_tree, dfd, filename, flags);
}

static inline int sys_move_mount(int from_dfd, const char *from_pathname, int to_dfd,
				 const char *to_pathname, unsigned int flags)
{
	return syscall(__NR_move_mount, from_dfd, from_pathname, to_dfd, to_pathname, flags);
}

static ssize_t write_nointr(int fd, const void *buf, size_t count)
{
	ssize_t ret;

	do {
		ret = write(fd, buf, count);
	} while (ret < 0 && errno == EINTR);

	return ret;
}

static int write_file(const char *path, const void *buf, size_t count)
{
	int fd;
	ssize_t ret;

	fd = open(path, O_WRONLY | O_CLOEXEC | O_NOCTTY | O_NOFOLLOW);
	if (fd < 0)
		return -1;

	ret = write_nointr(fd, buf, count);
	close(fd);
	if (ret < 0 || (size_t)ret != count)
		return -1;

	return 0;
}

/*
 * Let's use the "standard stack limit" (i.e. glibc thread size default) for
 * stack sizes: 8MB.
 */
#define __STACK_SIZE (8 * 1024 * 1024)
static pid_t do_clone(int (*fn)(void *), void *arg, int flags)
{
	void *stack;

	stack = malloc(__STACK_SIZE);
	if (!stack)
		return -ENOMEM;

#ifdef __ia64__
	return __clone2(fn, stack, __STACK_SIZE, flags | SIGCHLD, arg, NULL);
#else
	return clone(fn, stack + __STACK_SIZE, flags | SIGCHLD, arg, NULL);
#endif
}

static int clone_cb(void *data)
{
	return kill(getpid(), SIGSTOP);
}

struct list {
	void *elem;
	struct list *next;
	struct list *prev;
};

#define init_list(l)                 \
	{                            \
		.next = l, .prev = l \
	}

#define list_for_each(__iterator, __list) \
	for (__iterator = (__list)->next; __iterator != __list; __iterator = __iterator->next)

static inline void list_init(struct list *list)
{
	list->elem = NULL;
	list->next = list->prev = list;
}

static inline void list_add_elem(struct list *list, void *elem)
{
	list->elem = elem;
}

static inline void *list_first_elem(const struct list *list)
{
	return list->next->elem;
}

static inline void *list_last_elem(const struct list *list)
{
	return list->prev->elem;
}

static inline int list_empty(const struct list *list)
{
	return list == list->next;
}

static inline void __list_add(struct list *new, struct list *prev, struct list *next)
{
	next->prev = new;
	new->next = next;
	new->prev = prev;
	prev->next = new;
}

static inline void list_add(struct list *head, struct list *list)
{
	__list_add(list, head, head->next);
}

static inline void list_add_tail(struct list *head, struct list *list)
{
	__list_add(list, head->prev, head);
}

static inline void list_del(struct list *list)
{
	struct list *next, *prev;

	next = list->next;
	prev = list->prev;
	next->prev = prev;
	prev->next = next;
}

static inline size_t list_len(struct list *list)
{
	size_t i = 0;
	struct list *iter;

	list_for_each(iter, list)
	{
		i++;
	}

	return i;
}

enum idtype {
	ID_TYPE_UID,
	ID_TYPE_GID
};

struct id_map {
	enum idtype idtype;
	unsigned long nsid;
	unsigned long hostid;
	unsigned long range;
};

static struct list active_map;

static int add_map_entry(long host_id, long ns_id, long range, int which)
{
	struct list *tmp = NULL;
	struct id_map *newmap;

	newmap = malloc(sizeof(*newmap));
	if (!newmap)
		return -1;

	newmap->hostid = host_id;
	newmap->nsid = ns_id;
	newmap->range = range;
	newmap->idtype = which;
	tmp = malloc(sizeof(*tmp));
	if (!tmp) {
		free(newmap);
		return -1;
	}

	tmp->elem = newmap;
	list_add_tail(&active_map, tmp);
	return 0;
}

static int parse_map(char *map)
{
	int i, ret, idtype;
	long host_id, ns_id, range;
	char which;
	char types[2] = {'u', 'g'};

	if (!map)
		return -1;

	ret = sscanf(map, "%c:%ld:%ld:%ld", &which, &ns_id, &host_id, &range);
	if (ret != 4)
		return -1;

	if (which != 'b' && which != 'u' && which != 'g')
		return -1;

	for (i = 0; i < 2; i++) {
		if (which != types[i] && which != 'b')
			continue;

		if (types[i] == 'u')
			idtype = ID_TYPE_UID;
		else
			idtype = ID_TYPE_GID;

		ret = add_map_entry(host_id, ns_id, range, idtype);
		if (ret < 0)
			return ret;
	}

	return 0;
}

#define define_cleanup_function(type, cleaner)           \
	static inline void cleaner##_function(type *ptr) \
	{                                                \
		if (*ptr)                                \
			cleaner(*ptr);                   \
	}

#define call_cleaner(cleaner) __attribute__((__cleanup__(cleaner##_function)))

#define close_prot_errno_disarm(fd) \
	if (fd >= 0) {              \
		int _e_ = errno;    \
		close(fd);          \
		errno = _e_;        \
		fd = -EBADF;        \
	}

static inline void close_prot_errno_disarm_function(int *fd)
{
       close_prot_errno_disarm(*fd);
}
#define __do_close call_cleaner(close_prot_errno_disarm)

define_cleanup_function(FILE *, fclose);
#define __do_fclose call_cleaner(fclose)

define_cleanup_function(DIR *, closedir);
#define __do_closedir call_cleaner(closedir)

static int write_id_mapping(enum idtype idtype, pid_t pid, const char *buf, size_t buf_size)
{
	__do_close int fd = -EBADF;
	int ret;
	char path[PATH_MAX];

	if (geteuid() != 0 && idtype == ID_TYPE_GID) {
		__do_close int setgroups_fd = -EBADF;

		ret = snprintf(path, PATH_MAX, "/proc/%d/setgroups", pid);
		if (ret < 0 || ret >= PATH_MAX)
			return -E2BIG;

		setgroups_fd = open(path, O_WRONLY);
		if (setgroups_fd < 0 && errno != ENOENT)
			return log_error_errno(-1, errno, "Failed to open \"%s\"", path);

		if (setgroups_fd >= 0) {
			ret = write_nointr(setgroups_fd, "deny\n", STRLITERALLEN("deny\n"));
			if (ret != STRLITERALLEN("deny\n"))
				return log_error_errno(-1, errno, "Failed to write \"deny\" to \"/proc/%d/setgroups\"", pid);
		}
	}

	ret = snprintf(path, PATH_MAX, "/proc/%d/%cid_map", pid, idtype == ID_TYPE_UID ? 'u' : 'g');
	if (ret < 0 || ret >= PATH_MAX)
		return -E2BIG;

	fd = open(path, O_WRONLY | O_CLOEXEC);
	if (fd < 0)
		return log_error_errno(-1, errno, "Failed to open \"%s\"", path);

	ret = write_nointr(fd, buf, buf_size);
	if (ret != buf_size)
		return log_error_errno(-1, errno, "Failed to write %cid mapping to \"%s\"",
				       idtype == ID_TYPE_UID ? 'u' : 'g', path);

	return 0;
}

static int map_ids(struct list *idmap, pid_t pid)
{
	int fill, left;
	char u_or_g;
	char *pos;
	char cmd_output[PATH_MAX];
	struct id_map *map;
	struct list *iterator;
	enum idtype type;
	int ret = 0, gidmap = 0, uidmap = 0;
	char mapbuf[STRLITERALLEN("new@idmap") + STRLITERALLEN(" ") + INTTYPE_TO_STRLEN(pid_t) +
		    STRLITERALLEN(" ") + IDMAPLEN] = {0};
	bool had_entry = false;
	int hostuid, hostgid;

	hostuid = geteuid();
	hostgid = getegid();

	for (type = ID_TYPE_UID, u_or_g = 'u'; type <= ID_TYPE_GID;
	     type++, u_or_g = 'g') {
		pos = mapbuf;

		list_for_each(iterator, idmap) {
			map = iterator->elem;
			if (map->idtype != type)
				continue;

			had_entry = true;

			left = IDMAPLEN - (pos - mapbuf);
			fill = snprintf(pos, left, "%lu %lu %lu\n", map->nsid, map->hostid, map->range);
			/*
			 * The kernel only takes <= 4k for writes to
			 * /proc/<pid>/{g,u}id_map
			 */
			if (fill <= 0 || fill >= left)
				return log_error_errno(-1, -E2BIG, "Too many %cid mappings defined", u_or_g);

			pos += fill;
		}
		if (!had_entry)
			continue;

		ret = write_id_mapping(type, pid, mapbuf, pos - mapbuf);
		if (ret < 0)
			return log_error_errno(-1, errno, "Failed to write mapping: %s", mapbuf);

		memset(mapbuf, 0, sizeof(mapbuf));
	}

	return 0;
}

static int wait_for_pid(pid_t pid)
{
	int status, ret;

again:
	ret = waitpid(pid, &status, 0);
	if (ret == -1) {
		if (errno == EINTR)
			goto again;

		return -1;
	}

	if (!WIFEXITED(status))
		return -1;

	return WEXITSTATUS(status);
}

static int get_userns_fd(struct list *idmap)
{
	int ret;
	pid_t pid;
	char path[256];

	pid = do_clone(clone_cb, NULL, CLONE_NEWUSER | CLONE_NEWNS);
	if (pid < 0)
		return -errno;

	ret = map_ids(idmap, pid);
	if (ret < 0)
		return ret;

	snprintf(path, sizeof(path), "/proc/%d/ns/user", pid);
	ret = open(path, O_RDONLY | O_CLOEXEC);
	(void)kill(pid, SIGKILL);
	wait_for_pid(pid);
	return ret;
}

static void usage(void)
{
	fprintf(stderr, "Description:\n");
	fprintf(stderr, "    Create an id-mapped mount\n\n");

	fprintf(stderr, "Examples:\n");
	fprintf(stderr, "    # Create an idmapped mount of /source on /target with both ('b') uids and gids mapped\n");
	fprintf(stderr, "    mount2 --map-mount b:0:10000:10000 /source /target\n\n");
	fprintf(stderr, "    # Create an idmapped mount of /source on /target\n");
	fprintf(stderr, "    # with uids ('u') and gids ('g') mapped separately\n");
	fprintf(stderr, "    mount2 --map-mount u:0:10000:10000 g:0:20000:20000 /source /target\n\n");
	fprintf(stderr, "    # Create an idmapped mount of /source on /target\n");
	fprintf(stderr, "    # with both ('b') uids and gids mapped and a user namespace\n");
	fprintf(stderr, "    # with both ('b') uids and gids mapped\n");
	fprintf(stderr, "    mount2 --map-caller b:0:10000:10000 --map-mount b:0:10000:1000 /source /target\n\n");
	fprintf(stderr, "    # Create an idmapped mount of /source on /target\n");
	fprintf(stderr, "    # with uids ('u') gids ('g') mapped separately\n");
	fprintf(stderr, "    # and a user namespace with both ('b') uids and gids mapped\n");
	fprintf(stderr, "    mount2 --map-caller u:0:10000:10000 g:0:20000:20000 --map-mount b:0:10000:1000 /source /target\n");

	_exit(EXIT_SUCCESS);
}

static const struct option longopts[] = {
	{"map-mount",	required_argument,	0,	'a'},
	{"map-caller",	required_argument,	0,	'b'},
	{"help",	no_argument,		0,	'c'},
	NULL,
};

int main(int argc, char *argv[])
{
	int ret;
	int index = 0;
	const char *caller_idmap = NULL, *source = NULL, *target = NULL;
	char *const *new_argv;
	int new_argc;
	struct mount_attr attr = {};

	list_init(&active_map);
	while ((ret = getopt_long_only(argc, argv, "", longopts, &index)) != -1) {
		switch (ret) {
		case 'a':
			ret = parse_map(optarg);
			if (ret < 0) {
				fprintf(stderr, "Failed to parse idmaps for mount\n");
				_exit(EXIT_FAILURE);
			}
			break;
		case 'd':
			caller_idmap = optarg;
			break;
		case 'c':
			/* fallthrough */
		default:
			usage();
		}
	}

	if (list_empty(&active_map))
		fprintf(stderr, "No idmaps specified for the mount\n");

	new_argv = &argv[optind];
	new_argc = argc - optind;
	if (new_argc < 2) {
		fprintf(stderr, "Missing source or target mountpoint\n");
		exit(EXIT_FAILURE);
	}
	source = new_argv[0];
	target = new_argv[1];

	int fd = sys_open_tree(-EBADF, source, OPEN_TREE_CLONE | OPEN_TREE_CLOEXEC | AT_EMPTY_PATH);
	if (fd < 0) {
		fprintf(stderr, "%m - Failed to open %s\n", source);
		exit(EXIT_FAILURE);
	}

	if (!list_empty(&active_map)) {
		attr.userns = get_userns_fd(&active_map);
		if (attr.userns < 0) {
			fprintf(stderr, "%m - Failed to create user namespace\n");
			exit(EXIT_FAILURE);
		}
	}

	ret = sys_move_mount(fd, "", -EBADF, target, MOVE_MOUNT_F_EMPTY_PATH);
	if (ret < 0) {
		fprintf(stderr, "%m - Failed to attach mount to %s\n", target);
		exit(EXIT_FAILURE);
	}

	close(fd);
	if (!list_empty(&active_map)) {
		fd = open(target, O_RDONLY | O_CLOEXEC);
		if (fd < 0) {
			fprintf(stderr, "%m - Failed to open %s\n", target);
			exit(EXIT_FAILURE);
		}

		attr.attr_set = MOUNT_ATTR_SHIFT;
		ret = sys_mount_setattr(fd, "", AT_EMPTY_PATH | AT_RECURSIVE, &attr, sizeof(attr));
		if (ret < 0) {
			fprintf(stderr, "%m - Failed to change mount attributes\n");
			exit(EXIT_FAILURE);
		}
	}


	close(fd);

	if (caller_idmap)
		execlp("lxc-usernsexec", "lxc-usernsexec", "-m", caller_idmap, "bash", (char *)NULL);
	exit(EXIT_SUCCESS);
}