/*
 * Copyright (C) 2014 John Crispin <blogic@openwrt.org>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public License version 2.1
 * as published by the Free Software Foundation
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdint.h>
#include <getopt.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "libubi/libubi-tiny.h"

static int mtd_find_index(char *name)
{
	FILE *fp = fopen("/proc/mtd", "r");
	char line[256];
	char *index = NULL;

	if (!fp)
		return -1;

	while (!index && fgets(line, sizeof(line), fp)) {
		if (strstr(line, name)) {
			char *eol = strstr(line, ":");

			if (!eol)
				continue;

			*eol = '\0';
			index = &line[3];
		}
	}

	fclose(fp);

	if (!index)
		return -1;

	return atoi(index);
}

static int mtd_find(char *name, char *ret)
{
	int index = mtd_find_index(name);
	if (index < 0)
		return -1;

	sprintf(ret, "/dev/mtd%d", index);

	return 0;
}

static int ubi_find(libubi_t libubi, char *name, char *ret)
{
	int index = mtd_find_index(name);
	int ubi = 0;

	if (index < 0)
		return -1;

	if (mtd_num2ubi_dev(libubi, index, &ubi)) {
		fprintf(stderr, "failed to get ubi node for %s\n", name);
		return -1;
	}
	sprintf(ret, "/dev/ubi%d", ubi);

	return 0;
}

static int ubi_find_mtd(libubi_t libubi, char *name, char *ret)
{
	struct ubi_dev_info info;

	if (ubi_find(libubi, name, ret))
		return -1;

	if (ubi_get_dev_info(libubi, ret, &info))
		return -1;

	sprintf(ret, "/dev/mtd%d", info.mtd_num);

	return 0;
}

static int volume_find(libubi_t libubi, char *name, char *ret)
{
	int index = mtd_find_index(name);
	struct ubi_vol_info vol;
	int ubi = 0;

	if (index < 0)
		return -1;

	if (mtd_num2ubi_dev(libubi, index, &ubi)) {
		fprintf(stderr, "failed to get ubi node for %s\n", name);
		return -1;
	}

	if (ubi_get_vol_info1_nm(libubi, ubi, name, &vol)) {
		fprintf(stderr, "failed to get ubi volume info for %s\n", name);
		return -1;
	}

	sprintf(ret, "/dev/ubi%d_%d", ubi, vol.vol_id);

	return 0;
}

static int main_image(char *partition, char *image, char *overlay)
{
	libubi_t libubi;
	int err;
	char mtd[64];
	char part[64];
	char node[64];
	char volume[64];
	char _data[64];
	char *data = NULL;

	if (mtd_find(partition, part)) {
		fprintf(stderr, "failed to find mtd partition %s\n", partition);
		return -1;
	}
	if (overlay && !mtd_find(overlay, _data))
		data = _data;

	libubi = libubi_open();
	if (!libubi) {
		fprintf(stderr, "cannot open libubi");
		return -1;
	}

	if (ubi_find_mtd(libubi, partition, mtd)) {
		fprintf(stderr, "failed to find mtd parent %s\n", partition);
		return -1;
	}

	if (ubi_find(libubi, partition, node)) {
		fprintf(stderr, "failed to find ubi volume %s\n", partition);
		return -1;
	}

	if (volume_find(libubi, partition, volume)) {
		fprintf(stderr, "failed to find ubi volume %s\n", partition);
		return -1;
	}

	err = ubidetach(libubi, mtd);
	if (err) {
		fprintf(stderr, "cannot detach \"%s\"", mtd);
		return -1;
	}

	err = ubiattach(libubi, mtd);
	if (err) {
		fprintf(stderr, "cannot attach \"%s\"", mtd);
		return -1;
	}

	if (data) {
		err = ubirmvol(libubi, node, overlay);
		if (err) {
			fprintf(stderr, "cannot remove \"%s\"", node);
			return -1;
		}
	}

	err = ubiupdatevol(libubi, volume, image);
	if (err) {
		fprintf(stderr, "cannot update \"%s\"", volume);
		return -1;
	}

	if (overlay) {
		err = ubimkvol(libubi, node, overlay, 1);
		if (err) {
			fprintf(stderr, "cannot make \"%s\"", node);
			return -1;
		}
	}

	libubi_close(libubi);

	return err;
}

static int main_info(void)
{
	struct ubi_info info;
	libubi_t libubi;
	int i;

	libubi = libubi_open();
	if (!libubi) {
		fprintf(stderr, "cannot open libubi");
		return -1;
	}

	if (ubi_get_info(libubi, &info)) {
		fprintf(stderr, "failed to get info\n");
		return -1;
	}

	for (i = info.lowest_dev_num; i <= info.highest_dev_num; i++) {
		struct ubi_dev_info dinfo;
		char ubi[64];
		int j;

		sprintf(ubi, "/dev/ubi%d", i);
		if (ubi_get_dev_info(libubi, ubi, &dinfo))
			continue;
		printf("device - %s\n  size: %lldBytes\n  bad blocks: %d\n",
			&ubi[5], dinfo.total_bytes, dinfo.bad_count);
		for (j = dinfo.lowest_vol_id; j <= dinfo.highest_vol_id; j++) {
			struct ubi_vol_info vinfo;

			sprintf(ubi, "/dev/ubi%d_%d", i, j);
			if (ubi_get_vol_info(libubi, ubi, &vinfo))
				continue;
			printf("  volume - %s\n", &ubi[5]);
			printf("\tname: %s\n", vinfo.name);
			printf("\tsize: %lld\n", vinfo.data_bytes);
		}
	}

	libubi_close(libubi);

	return 0;
}

static int print_usage(void)
{
	printf("ubi info\n");
	printf("ubi kernel <image.kernel.ubifs>\n");
	printf("ubi rootfs <image.rootfs.ubifs>\n");
	printf("ubi overlay <image.rootfs-overlay.ubifs>\n");

	return -1;
}

int main(int argc, char **argv)
{
	if (argc > 1 && !strcmp(argv[1], "info"))
		return main_info();

	if (argc < 3)
		return print_usage();

	if (!strcmp(argv[1], "kernel")) {
		return main_image("kernel", argv[2], NULL);

	} else if (!strcmp(argv[1], "rootfs")) {
		return main_image("rootfs", argv[2], NULL);

	} else if (!strcmp(argv[1], "overlay")) {
		return main_image("rootfs", argv[2], "rootfs_data");
	}

	return -1;
}

