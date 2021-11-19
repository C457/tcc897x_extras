//+[TCCQB] QB Disk Recovery
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/reboot.h>
#include <sys/wait.h>
#include <cutils/android_reboot.h>
#include <cutils/partition_utils.h>

const char *mkfs = "/system/bin/make_ext4fs";
const char *TAG = "qb_setup_fs";

#define USE_QB_PTN_RESTORE			// QuickBoot DISK Recovery Option.
									// It works with QuickBoot Booting. Normal Boot is not affected by this feature.

#ifdef USE_QB_PTN_RESTORE
/****************************************************************************
 *																			*
 *			Cache Partition should be the first argument.					*
 *			( Cache Partition should be checked first. )					*
 *			Refer to /device/telechips/xxxx/init.xxxx.setupfs.rc 			*
 *																			*
 ***************************************************************************/

#include <cutils/log.h>
#include <fcntl.h>
#include <cutils/properties.h>
#include <sys/mount.h>

#include <cutils/compiler.h>
#define unlikely(x...)	CC_UNLIKELY(x)

#include <cutils/qb_manager.h>	// For QuickBoot Properties, QB functions ( uartshow() ).


#define THIS_VERSION				"v0.10"


#define USERDATA_DEV				"/dev/block/platform/bdm/by-name/data"
#define CACHE_DEV					"/dev/block/platform/bdm/by-name/cache"
#define CACHE_ROOT					"/cache"
#define QB_DATA_DEV					"/dev/block/platform/bdm/by-name/qb_data"
#define QB_DATA_ROOT				"/cache/qb_data/"

#define COMMAND_RECOVERY_DIR		"/cache/recovery/"
#define COMMAND_RECOVERY_FILE		"/cache/recovery/command"
#define COMMAND_RECOVER_USERDATA	"--wipe_data\n--locale=en_US"

#define BUF_SIZE					1024


static int restore_userdata_ptn(const char *blockdev)
{
	/*		Cache partition should be mounted before this function is called.	*/
	int error = 0;
	fprintf(stderr,"%s : %s - Restore %s partition from default. \n", __FILE__, __func__, blockdev);

	/*      Create Recovery Command file's PATH DIR     */
	mkdir(COMMAND_RECOVERY_DIR, 0777);


	/*      Write Command to Command File       */
	int fd = 0;
	fd = open(COMMAND_RECOVERY_FILE, O_RDWR|O_CREAT|O_TRUNC|O_SYNC, 0600);
	if (fd < 0) {
		ALOGE("[%s] %s : open %s Failed!!\n", __FILE__, __func__,  COMMAND_RECOVERY_FILE);
		return -1;
	}
	write(fd, COMMAND_RECOVER_USERDATA, strlen(COMMAND_RECOVER_USERDATA) + 1);
	close(fd);
	uartshow("%s: Userdata Partition is damaged.Reboot with recovery mode to Recover USERDATA.", TAG);
	android_reboot(ANDROID_RB_RESTART2, 0, "recovery");

	return 0;
}
#endif  // USE_QB_PTN_RESTORE

int qb_setup_fs(const char *blockdev)
{
	char buf[256], path[128], blockdev_orig[256];
	pid_t child;
	int status, n, error = 0;
	pid_t pid;

#ifdef USE_QB_PTN_RESTORE
	/*		QB_DATA Partition is not exist at Storage.		*/
	if (!strcmp(blockdev, QB_DATA_DEV) && access(QB_DATA_DEV, F_OK) != 0) {
		return 0;
	}

	strcpy(blockdev_orig, blockdev);    // save blockdev before changed it

	if (!strcmp(blockdev_orig, QB_DATA_DEV) && access(QB_DATA_DEV, F_OK) == 0) {
		/*		If blockdev is QB_DATA, Mount it to check it's FileSystem.		*/
		/*		To Check Some Partition, it is necessary to be mounted.
		 *		QB_Data is not mounted normally, so Mount QB_data Partition to /cache/qb_data/.
		 *		To mount QB_data to /cache/qb_data/, Cache Partition is necessary to be mounted, too.
		 */
		mkdir(QB_DATA_ROOT, 0777);

		error = mount(QB_DATA_DEV, QB_DATA_ROOT, "ext4", MS_NOATIME | MS_NODEV | MS_NODIRATIME, "");
		if (error < 0) {
			fprintf(stderr, "error: qb_setup_fs: QB_DATA: mount failed!\n");
		}
	}
#else
	if (!strcmp(blockdev_orig, QB_DATA_DEV)) {
		/*		USE_QB_PTN_RESTORE is not enabled & blockdev is QB_DATA .	*/
		return 0;
	}
#endif

	/* we might be looking at an indirect reference */
	n = readlink(blockdev, path, sizeof(path) - 1);
	if (n > 0) {
		path[n] = 0;
		if (!memcmp(path, "/dev/block/", 11))
			blockdev = path + 11;
	}

	if (strchr(blockdev,'/')) {
		fprintf(stderr,"not a block device name: %s\n", blockdev);
		return 0;
	}

	snprintf(buf, sizeof(buf), "/sys/fs/ext4/%s", blockdev);
	if (access(buf, F_OK) == 0) {
		/*		This blockdev is mounted.	*/
		fprintf(stderr,"device %s already has a filesystem\n", blockdev);
#ifdef USE_QB_PTN_RESTORE
		if (!strcmp(blockdev_orig, QB_DATA_DEV) && access(QB_DATA_DEV, F_OK) == 0) {
			umount(QB_DATA_ROOT);
		}

		if (!strcmp(blockdev_orig, USERDATA_DEV)) {
			check_data_of_userdata();	// Check USERDATA
		}   
#endif

		return 0;
	}
	snprintf(buf, sizeof(buf), "/dev/block/%s", blockdev);

#ifdef USE_QB_PTN_RESTORE
	if (!strcmp(blockdev_orig, QB_DATA_DEV) && access(QB_DATA_DEV, F_OK) == 0) {
		umount(QB_DATA_ROOT);
	}

	if (!strcmp(blockdev_orig, USERDATA_DEV)) {
		/*      QuickBoot Booting & USERDATA DEV        */
		int error;
		error = restore_userdata_ptn(blockdev_orig);
		return error;
	}
#else
	if (!partition_wiped(buf)) {
		fprintf(stderr,"device %s not wiped, probably encrypted, not wiping\n", blockdev);
		return 0;
	}
#endif

	fprintf(stderr,"+++\n");

	child = fork();
	if (child < 0) {
		fprintf(stderr,"error: qb_setup_fs: fork failed\n");
		return 0;
	}
	if (child == 0) {
		execl(mkfs, mkfs, buf, NULL);
		exit(-1);
	}

	while ((pid=waitpid(-1, &status, 0)) != child) {
		if (pid == -1) {
			fprintf(stderr, "error: qb_setup_fs: waitpid failed!\n");
			return 1;
		}
	}

#ifdef USE_QB_PTN_RESTORE
	uartshow("%s: Complete to format[%s]", TAG, blockdev_orig);

	if (!strcmp(blockdev_orig, CACHE_DEV)) {
		uartshow("%s: System Will Reboot to mount partitions in QB fstab.", TAG);

		android_reboot(ANDROID_RB_RESTART, 0, 0); // Reboot system.
	} else if (!strcmp(blockdev_orig, QB_DATA_DEV)) {
		return 0;	// Prevent to reboot system reboot.
	}
#endif

	fprintf(stderr,"---\n");
	return 1;
}


int main(int argc, char **argv)
{
#ifndef USE_QB_PTN_RESTORE
	/*		If 'USE_QB_PTN_RESTORE' is disabled, Finish qb_setup_fs.		*/
	return 0;
#else
	char boot_mode[128];
	property_get(QBMANAGER_BOOT_WITH, boot_mode, "");
	if(!strcmp("snapshot", boot_mode) || !strcmp("hibernate", boot_mode)) {
		uartshow("%s: Start qb_setup_fs. %s", TAG, THIS_VERSION);
	} else {
		uartshow("%s: Exit qb_setup_fs by tcc.QB.boot.with. It's not QuickBoot.", TAG);
		return 0;
	}
#endif

	int need_reboot = 0;

	while (argc > 1) {
		if (strlen(argv[1]) < 128)
			need_reboot |= qb_setup_fs(argv[1]);
		argv++;
		argc--;
	}

	if (need_reboot) {
		fprintf(stderr,"REBOOT!\n");
		android_reboot(ANDROID_RB_RESTART, 0, 0);
		exit(-1);
	}
	
	uartshow("%s: Finished qb_setup_fs.", TAG);

	return 0;
}


#ifdef USE_QB_PTN_RESTORE
/****************************************************************************
 *																			*
 *			Check USERDATA if all sub dirs are exist or not.				*
 *			Also, Check some critical files.								*
 *																			*
 ***************************************************************************/

/*	The list which is needed to boot with Normal Boot to recover userdata.
 *	1. /data/media/0
 *	2. /data/system/
 *	3. /data/user/0
 *	4. /data/data/ ( it is connected by /data/user/0. It is not needed to check. )
 *	5. sub files of /data/dalvik-cache/.
 *	6. sub files of /data/system/.	
 *	   ( But if only some files are deleted, Normal boot cannot create deleted files.
 *	     So, should delete /data/system/ dir. )
 */

#define DATA_DIR_LIST 	"/data/hibernate/data_dir.list"
#define DATA_FILE_LIST 	"/data/hibernate/data_file.list"
#define PKG_LIST_FILE	"/data/system/packages.list"

/*	Check all files which is in the listed dirs.
 *	If some files are not exists, reboot with normal boot.	*/
#define CHECK_DALVIK_LIST	"/data/dalvik-cache/"
#define CHECK_SYSTEM_LIST	"/data/system/"

#define PROPERTY_DIR		"/data/property/"

#define SYSTEM_UID_MAX		200		// The number of Fixed UIDs.
#define UID_START			10000

struct userAppInfo {
	char *pkgName;
	int pkgUid;
	struct userAppInfo *next;
};

struct userAppInfo rootElem = {NULL, 0, NULL};
struct userAppInfo *rootUAI = &rootElem;

enum EXCUTE_LIST {
	MKDIR,
	RM
};

/*		chec dir & file list		*/
static char *userdata_list[] = {
	DATA_DIR_LIST,
	DATA_FILE_LIST,
};

/*		/data/ critical dir & file list		*/
static char *critical_list[] = {
	"/data/media/0",
	"/data/user/0",
	"/data/system/",
};

int check_data_of_userdata(void)
{
	int ret = 0;
	int list_idx = 0;

	/*		Check if Critical files are exists or not.		*/
	for (list_idx = 0; list_idx < sizeof(critical_list)/sizeof(critical_list[0]); list_idx++) {
		if (unlikely(access(critical_list[list_idx], F_OK) != 0)) {
			mkdir(PROPERTY_DIR, 0777);	// Create propery dir to set property.
			// Skip QB Popup if system is boot with normal by recovery mode.
			property_set(QBMANAGER_SKIP_BOOT, "true");
			uartshow("%s: Userdata Partition has problem[%s] Reboot to fix it.", TAG, critical_list[list_idx]);
			android_reboot(ANDROID_RB_RESTART2, 0, "force_normal");
		}
	}

	return 0;
}
#endif	// USE_QB_PTN_RESTORE
//-[TCCQB]
//
