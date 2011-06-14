/* libnetmd.c
 *      Copyright (C) 2002, 2003 Marc Britten
 *
 * This file is part of libnetmd.
 *
 * libnetmd is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * Libnetmd is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA
 */

#include "libnetmd.h"

int min(int a,int b)
{
	if (a<b) return a;
	return b;
}

/*! list of known codecs (mapped to protocol ID) that can be used in NetMD devices */
/*! Bertrik: the original interpretation of these numbers as codecs appears incorrect.
    These values look like track protection values instead */
struct netmd_pair const trprot_settings[] = 
{
	{0x00, "UnPROT"},
	{0x03, "TrPROT"},
	{0, 0} /* terminating pair */
};

/*! list of known bitrates (mapped to protocol ID) that can be used in NetMD devices */
struct netmd_pair const bitrates[] =
{
	{0x90, "Stereo"},
	{0x92, "LP2"},
	{0x93, "LP4"},
	{0, 0} /* terminating pair */
};

struct netmd_pair const unknown_pair = {0x00, "UNKNOWN"};

struct netmd_pair const* find_pair(int hex, struct netmd_pair const* array)
{
	int i = 0;
	for(; array[i].name != NULL; i++)
	{
		if(array[i].hex == hex)
			return &array[i];
	}

	return &unknown_pair;
}

static void waitforsync(usb_dev_handle* dev)
{
  char syncmsg[4];
  fprintf(stderr,"Waiting for Sync: \n");
  do {
    usb_control_msg(dev, 0xc1, 0x01, 0, 0, syncmsg, 0x04, 5000);
  } while  (memcmp(syncmsg,"\0\0\0\0",4)!=0);

}

static char* sendcommand(netmd_dev_handle* devh, char* str, int len, unsigned char* response, int rlen)
{
	int i, ret, size = 0;
	static char buf[256];
  
	ret = netmd_exch_message(devh, str, len, buf);
	if (ret < 0) {
		fprintf(stderr, "bad ret code, returning early\n");
		return NULL;
	}
	
	// Calculate difference to expected response
	if (response != NULL) {
		int c=0;
		for (i=0; i < min(rlen, size); i++) {
			if (response[i] != buf[i]) {
				c++;
			}
		}
		fprintf(stderr, "Differ: %d\n",c);
	}

	return buf;
}

static int request_disc_title(netmd_dev_handle* dev, char* buffer, int size)
{
	int ret = -1;
	int title_size = 0;
	char title_request[0x13] = {0x00, 0x18, 0x06, 0x02, 0x20, 0x18, 0x01, 0x00, 0x00, 0x30, 0x00, 0xa, 0x00, 0xff, 0x00, 0x00, 0x00, 0x00, 0x00};
	char title[255];

	ret = netmd_exch_message(dev, title_request, 0x13, title);
	if(ret < 0)
	{
		fprintf(stderr, "request_disc_title: bad ret code, returning early\n");
		return 0;
	}

	title_size = ret;

	if(title_size == 0 || title_size == 0x13)
		return -1; /* bail early somethings wrong */

	if(title_size > size)
	{
		printf("request_disc_title: title too large for buffer\n");
		return -1;
	}

	memset(buffer, 0, size);
	strncpy(buffer, (title + 25), title_size - 25);
	/* buffer[size + 1] = 0;   XXX Huh? This is outside the bounds passed in! */

	return title_size - 25;
}

int netmd_request_track_bitrate(netmd_dev_handle*dev, int track, unsigned char* data)
{
	int ret = 0;
	int size = 0;
	char request[] = {0x00, 0x18, 0x06, 0x02, 0x20, 0x10, 0x01, 0x00, 0xDD, 0x30, 0x80, 0x07, 0x00, 0xff, 0x00, 0x00, 0x00, 0x00, 0x00};
	char reply[255];

	request[8] = track;
	ret = netmd_exch_message(dev, request, 0x13, reply);
	if(ret < 0)
	{
		fprintf(stderr, "bad ret code, returning early\n");
		return 0;
	}

	size = ret;

	*data = reply[size - 2];
	return ret;
}

int netmd_request_track_flags(netmd_dev_handle*dev, int track, char* data)
{
	int ret = 0;
	int size = 0;
	char request[] = {0x00, 0x18, 0x06, 0x01, 0x20, 0x10, 0x01, 0x00, 0xDD, 0xff, 0x00, 0x00, 0x01, 0x00, 0x08};
	char reply[255];

	request[8] = track;
	ret = netmd_exch_message(dev, request, 15, reply);
	if(ret < 0)
	{
		fprintf(stderr, "bad ret code, returning early\n");
		return 0;
	}

	size = ret;

	*data = reply[size - 1];
	return ret;
}

/*  equiv. of
 	sprintf(tmp, "%02x", time_request[ 	time = strtol(tmp, NULL, 10); */
#define BCD_TO_PROPER(x) (((((x) & 0xf0) >> 4) * 10) + ((x) & 0x0f))

int netmd_request_track_time(netmd_dev_handle* dev, int track, struct netmd_track* buffer)
{
	int ret = 0;
	int size = 0;
	char request[] = {0x00, 0x18, 0x06, 0x02, 0x20, 0x10, 0x01, 0x00, 0x01, 0x30, 0x00, 0x01, 0x00, 0xff, 0x00, 0x00, 0x00, 0x00, 0x00};
	char time_request[255];

	request[8] = track;
	ret = netmd_exch_message(dev, request, 0x13, time_request);
	if(ret < 0)
	{
		fprintf(stderr, "bad ret code, returning early\n");
		return 0;
	}

	size = ret;

	buffer->minute = BCD_TO_PROPER(time_request[28]);
	buffer->second = BCD_TO_PROPER(time_request[29]);
	buffer->tenth = BCD_TO_PROPER(time_request[30]);
	buffer->track = track;

	return 1;
}

int netmd_request_title(netmd_dev_handle* dev, int track, char* buffer, int size)
{
	int ret = -1;
	int title_size = 0;
	char title_request[0x13] = {0x00, 0x18, 0x06, 0x02, 0x20, 0x18, 0x02, 0x00, 0x00, 0x30, 0x00, 0xa, 0x00, 0xff, 0x00, 0x00, 0x00, 0x00, 0x00};
	char title[255];

	title_request[8] = track;
	ret = netmd_exch_message(dev, title_request, 0x13, title);
	if(ret < 0)
	{
		fprintf(stderr, "bad ret code, returning early\n");
		return -1;
	}

	title_size = ret;

	if(title_size == 0 || title_size == 0x13)
		return -1; /* bail early somethings wrong or no track */

	if(title_size > size)
	{
		printf("netmd_request_title: title too large for buffer\n");
		return -1;
	}

	memset(buffer, 0, size);
	strncpy(buffer, (title + 25), title_size - 25);
	buffer[size + 1] = 0;

	return title_size - 25;
}

int netmd_set_title(netmd_dev_handle* dev, int track, char* buffer)
{
	int ret = 1;
	char *title_request = NULL;
	char title_header[21] = {0x00, 0x18, 0x07, 0x02, 0x20, 0x18, 0x02, 0x00,
				 0x00, 0x30, 0x00, 0x0a, 0x00, 0x50, 0x00, 0x00,
				 0x0a, 0x00, 0x00, 0x00, 0x0d};
	char reply[255];
	int size;

	size = strlen(buffer);
	title_request = malloc(sizeof(char) * (0x15 + size));
	memcpy(title_request, title_header, 0x15);
	memcpy((title_request + 0x15), buffer, size);

	title_request[8] = track;
	title_request[16] = size;
	title_request[20] = size;

	ret = netmd_exch_message(dev, title_request, (int)(0x15 + size), reply);
	if(ret < 0)
	{
		fprintf(stderr, "bad ret code, returning early\n");
		return 0;
	}

	free(title_request);
	return 1;
}

int netmd_move_track(netmd_dev_handle* dev, int start, int finish)
{
	int ret = 0;
	char request[] = {0x00, 0x18, 0x43, 0xff, 0x00, 0x00, 0x20, 0x10, 0x01, 0x00, 0x04, 0x20, 0x10, 0x01, 0x00, 0x03};
	char reply[255];

	request[10] = start;
	request[15] = finish;

	ret = netmd_exch_message(dev, request, 16, reply);

	if(ret < 0)
	{
		fprintf(stderr, "bad ret code, returning early\n");
		return 0;
	}

	return 1;
}

static int get_group_count(netmd_dev_handle* devh, minidisc* md)
{

	int disc_size = 0;
	int track;
	char disc[256];
	char *tok = 0;
	char *next_tok;
	char *semicolon;		/* Pointers to markers in group data */

	int g = 0;

	disc_size = request_disc_title(devh, disc, 256);
	md->header_length = disc_size;

	if(disc_size != 0)
	{
		track = strtol(disc, NULL, 10); // returns 0 on 0 or non conversion

		tok = disc;
		next_tok = strstr(disc, "//");
		while(0 != next_tok)
		{
			*next_tok = 0;
			next_tok += 2;

			semicolon = strchr( tok, ';' );
			if((!track && g == 0) && tok[0] != ';') {
			}
			else
			{
				if(g == 0) {
					g++;
					continue;
				}

				/* Terminate string at the semicolon for easier parsing */
			}
			g++;
			tok = next_tok;
			next_tok = strstr(tok, "//");
		}
	}

	return (g);
}

int netmd_set_group_title(netmd_dev_handle* dev, minidisc* md, int group, char* title)
{
	int size = strlen(title);

	md->groups[group].name = realloc(md->groups[group].name, size + 1);

	if(md->groups[group].name != 0)
		strcpy(md->groups[group].name, title);
	else
		return 0;

	netmd_write_disc_header(dev, md);

	return 1;
}

static void set_group_data(minidisc* md, int group, char* name, int start, int finish) {
	md->groups[group].name = strdup( name );
	md->groups[group].start = start;
	md->groups[group].finish = finish;
	return;
}

	/* Sonys code is utter bile. So far we've encountered the following first segments in the disc title:
	 *
	 * 0[-n];<title>// - titled disc.
	 * <title>// - titled disc
	 * 0[-n];// - untitled disc
	 * n{n>0};<group>// - untitled disc, group with one track
	 * n{n>0}-n2{n2>n>0};group// - untitled disc, group with multiple tracks
	 * ;group// - untitled disc, group with no tracks
	 *
	 */

int netmd_initialize_disc_info(netmd_dev_handle* devh, minidisc* md)
{
	int disc_size = 0;
	int track;
	char disc[256];
	char *tok = 0;
	char *next_tok;
	char *hyphen, *semicolon;		/* Pointers to markers in group data */
	char *name;

	int start, finish;
	int g = 0;

	md->group_count = get_group_count(devh, md);

	/* You always have at least one group, the disc title */
	if(md->group_count == 0)
		md->group_count++;


	md->groups = malloc(sizeof(struct netmd_group) * md->group_count);
	memset(md->groups, 0, sizeof(struct netmd_group) * md->group_count);

	disc_size = request_disc_title(devh, disc, 256);
	printf("Raw title: %s \n", disc);
	md->header_length = disc_size;

	if(disc_size != 0)
	{
		track = strtol(disc, NULL, 10); // returns 0 on 0 or non conversion

		tok = disc;
		next_tok = strstr(disc, "//");
		while(0 != next_tok)
		{
			*next_tok = 0;
			next_tok += 2;

			semicolon = strchr( tok, ';' );
			if((!track && g == 0) && tok[0] != ';') {

				if(semicolon == NULL) {
					/* <title>// */
					name = tok;
					start = 0;
					finish = 0;

					set_group_data(md, 0, name, start, finish);
				}
				else
				{
					/* TODO: following code looks buggy: unreachable */
					if(semicolon == 0)
					{
						/* ;group// */
						name = strdup("<Untitled>");
						start = 0;
						finish = 0;
						set_group_data(md, 0, name, start, finish);
						g++;
						name = semicolon + 1;
						set_group_data(md, g, name, start, finish);
					}
					else
					{
						//0[-n];<title>//
						name = semicolon + 1;
						set_group_data(md, 0, name, 0, 0);
					}
				}
			}
			else
			{
				if(g == 0) {
					name = strdup("<Untitled>");
					start = 0;
					finish = 0;
					set_group_data(md, 0, name, start, finish);

					g++;
					continue;
				}

				/* Terminate string at the semicolon for easier parsing */
				name = semicolon + 1;
				hyphen = strchr( tok, '-' );
				if( hyphen )
				{
					*hyphen = '\0';
					start = atoi( tok );
					finish = atoi( hyphen + 1 );
				}
				else
				{
					start = atoi( tok );
					finish = start;
				}

				if( finish < start )
				{
					printf( "Title parse error\n" );
					return -1;
				}
				set_group_data(md, g, name, start, finish);
			}
			g++;
			tok = next_tok;
			next_tok = strstr(tok, "//");
		}
	}
	return disc_size;
}

void print_groups(minidisc *md)
{
	int i = 0;
	for(;i < md->group_count; i++)
	{
		printf("Group %i: %s - %i - %i\n", i, md->groups[i].name, md->groups[i].start, md->groups[i].finish);
	}
	printf("\n");
}

int netmd_create_group(netmd_dev_handle* devh, char* name)
{
	int disc_size;
	int seperator = 0;
	char* disc = malloc(sizeof(char) * 60);
	char* new_title = 0;
	char* p = 0;
	char* request = 0;
	char reply[255];
	int ret;
	char write_req[] = {0x00, 0x18, 0x07, 0x02, 0x20, 0x18, 0x01, 0x00, 0x00, 0x30, 0x00, 0x0a, 0x00, 0x50, 0x00, 0x00};

	disc_size = request_disc_title(devh, disc, 60);

	if(disc_size > 60)
	{
		disc = realloc(disc, disc_size);
		disc_size = request_disc_title(devh, disc, 60);
	}

	seperator = strlen(disc);
	if(disc[0] != '0')
	{
		disc_size += 2;
		seperator += 2;
	}

	p = disc + strlen(disc) - 2;
	if(strcmp(p, "//") != 0)
		disc_size += 2;

	/* need a ; // and name added */
	disc_size += (strlen(name) + 3);
	new_title = malloc(disc_size);

	memset(new_title, 0, disc_size);
	if(disc[0] != '0')
	{
		new_title[0] = '0';
		new_title[1] = ';';
	}
	strcat(new_title, disc);

 	if(strcmp(p, "//") != 0)
	{
		strcat(new_title, "//");
		seperator += 2;
	}

	new_title[seperator] = ';';

	strcat(new_title, name);
	strcat(new_title, "//");

	printf("%s\n", new_title);

	request = malloc(21 + disc_size);
	memset(request, 0, 21 + disc_size);

	memcpy(request, write_req, 16);
	request[16] = disc_size;
	request[20] = disc_size;

	p = request + 21;
	memcpy(p, new_title, disc_size);

	ret = netmd_exch_message(devh, request, (int)(0x15 + disc_size), reply);
	return ret;
}

/* move track, then manipulate title string */
int netmd_put_track_in_group(netmd_dev_handle* dev, minidisc *md, int track, int group)
{
	int i = 0;
	int j = 0;
	int found = 0;

	printf("netmd_put_track_in_group(dev, %i, %i)\nGroup Count %i\n", track, group, md->group_count);

	if(group >= md->group_count)
	{
		return 0;
	}

	print_groups(md);

	/* remove track from old group */
	for(i = 0; i < md->group_count; i++)
	{
		if(i == 0)
		{
			/* if track is before first real group */
			if(md->groups[i+1].start == 0)
			{
				/* nothing in group  */
				found = 1;
			}
			if((track + 1) < md->groups[i+1].start)
			{
				found = 1;
				for(j = i+1; j < md->group_count; j++)
				{
					md->groups[j].start--;
					if(md->groups[j].finish != 0)
						md->groups[j].finish--;
				}
			}
		}
		else if(md->groups[i].start <= (track + 1) && md->groups[i].finish >= (track + 1))
		{
			found = 1;
			/* decrement start/finish for all following groups */
			for(j = i+1; j < md->group_count; j++)
			{
				md->groups[j].start--;
				if(md->groups[j].finish != 0)
					md->groups[j].finish--;
			}
		}
	}

	/* if track is in between groups */
	if(!found)
	{
		for(i = 2; i < md->group_count; i++)
		{
			if(md->groups[i].start >= (track+1) && md->groups[i-1].finish <= (track+1))
			{
				found = 1;
				/* decrement start/finish for all groups including and after this one */
				for(j = i; j < md->group_count; j++)
				{
					md->groups[j].start--;
					if(md->groups[j].finish != 0)
						md->groups[j].finish--;
				}
			}
		}
	}

	print_groups(md);

	/* insert track into group range */
	if(md->groups[group].finish != 0)
	{
		md->groups[group].finish++;
	}
	else
	{
		if(md->groups[group].start == 0)
			md->groups[group].start = track + 1;
		else
			md->groups[group].finish = md->groups[group].start + 1;
	}

	/* if not last group */
	if((group + 1) < md->group_count)
	{
		int j = 0;
		for(j = group + 1; j < md->group_count; j++)
		{
			/* if group is NOT empty */
			if(md->groups[j].start != 0 || md->groups[j].finish != 0)
			{
				md->groups[j].start++;
				if(md->groups[j].finish != 0)
				{
					md->groups[j].finish++;
				}
			}
		}
	}

	/* what does it look like now? */
	print_groups(md);

	if(md->groups[group].finish != 0)
	{
	   	netmd_move_track(dev, track, md->groups[group].finish - 1);
	}
	else
	{
		if(md->groups[group].start != 0)
			netmd_move_track(dev, track, md->groups[group].start - 1);
		else
			netmd_move_track(dev, track, md->groups[group].start);
	}

	return netmd_write_disc_header(dev, md);
}

int netmd_move_group(netmd_dev_handle* dev, minidisc* md, int track, int group)
{
	int index = 0;
	int i = 0;
	int gs = 0;
 	struct netmd_group store1;
	struct netmd_group *p, *p2;
	int gt = md->groups[group].start;
	int finish = (md->groups[group].finish - md->groups[group].start) + track;

	p = p2 = 0;

	// empty groups can't be in front
	if(gt == 0)
		return -1;

	/* loop, moving tracks to new positions */
	for(index = track; index <= finish; index++, gt++)
	{
		printf("Moving track %i to %i\n", (gt - 1), index);
		netmd_move_track(dev, (gt -1), index);
	}
	md->groups[group].start = track + 1;
	md->groups[group].finish = index;

	/* create a copy of groups */
	p = malloc(sizeof(struct netmd_group) * md->group_count);
	for(index = 0; index < md->group_count; index++)
	{
		p[index].name = malloc(strlen(md->groups[index].name) + 1);
		strcpy(p[index].name, md->groups[index].name);
		p[index].start = md->groups[index].start;
		p[index].finish = md->groups[index].finish;
	}

	store1 = p[group];
	gs = store1.finish - store1.start + 1; /* how many tracks got moved? */

	/* find group to bump */
	if(track < md->groups[group].start)
	{
		for(index = 0; index < md->group_count; index++)
		{
			if(md->groups[index].start > track)
			{
				for(i = group - 1; i >= index; i--)
				{
					/* all tracks get moved gs spots */
					p[i].start += gs;

					if(p[i].finish != 0)
						p[i].finish += gs;

					p[i + 1] = p[i]; /* bump group down the list */
				}

				p[index] = store1;
				break;
			}
			else
			{
				if((group + 1) < md->group_count)
				{
					for(i = group + 1; i < md->group_count; i++)
					{
						/* all tracks get moved gs spots */
						p[i].start -= gs;

						if(p[i].finish != 0)
							p[i].finish -= gs;

						p[i - 1] = p[i]; /* bump group down the list */
					}

					p[index] = store1;
					break;
				}
			}
		}
	}

	/* free all memory, then make our copy the real info */
	netmd_clean_disc_info(md);
	md->groups = p;

	netmd_write_disc_header(dev, md);
	return 0;
}

int netmd_delete_group(netmd_dev_handle* dev, minidisc* md, int group)
{
	int index = 0;
	struct netmd_group *p;

	/* check if requested group exists */
	if((group < 0) || (group > md->group_count))
		return -1;

	/* create a copy of groups below requested group */
	p = malloc(sizeof(struct netmd_group) * md->group_count);
	for(index = 0; index < group; index++)
	{
		p[index].name = strdup(md->groups[index].name);
		p[index].start = md->groups[index].start;
		p[index].finish = md->groups[index].finish;
	}

	/* copy groups above requested group */
	for(; index < md->group_count-1; index++)
	{
		p[index].name = strdup(md->groups[index+1].name);
		p[index].start = md->groups[index+1].start;
		p[index].finish = md->groups[index+1].finish;
	}

	/* free all memory, then make our copy the real info */
	netmd_clean_disc_info(md);
	md->groups = p;

	/* one less group now */
	md->group_count--;

	netmd_write_disc_header(dev, md);
	return 0;
}

int netmd_set_track( netmd_dev_handle* dev, int track)
{
	int ret = 0;
	char request[] = {0x00, 0x18, 0x50, 0xff, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};  // Change track
	int size;
	char buf[255];

	fprintf(stderr,"Selecting track: %d \n",track);
	request[10] = track-1;
	ret = netmd_exch_message(dev, request, 11, buf);

	if(ret < 0)
	{
		fprintf(stderr, "bad ret code, returning early\n");
		return 0;
	}

	size = ret;
	if (size<1) {
		fprintf(stderr, "Invalid size\n");
		return -1;
	}

	return 1;
}

#define NETMD_PLAY	0x75
#define NETMD_PAUSE	0x7d
#define NETMD_REWIND	0x49
#define NETMD_FFORWARD	0x39

static int netmd_playback_control(netmd_dev_handle* dev, unsigned char code)
{
	int ret = 0;
	char request[] = {0x00, 0x18, 0xc3, 0xff, 0x75, 0x00, 0x00, 0x00};                   // Play
	int size;
	char buf[255];

	request[4] = code;

	ret = netmd_exch_message(dev, request, 8, buf);

	size = ret;
	if (size<1) {
		fprintf(stderr, "Invalid size\n");
		return -1;
	}

	return 1;
}


int netmd_play( netmd_dev_handle *dev )
{

	fprintf(stderr,"Starting playback\n");
	return netmd_playback_control( dev, NETMD_PLAY );
}

int netmd_pause( netmd_dev_handle *dev )
{

	fprintf(stderr,"Pausing\n");
	return netmd_playback_control( dev, NETMD_PAUSE );
}

int netmd_fast_forward( netmd_dev_handle *dev )
{

	fprintf(stderr,"Starting fast forward\n");
	return netmd_playback_control( dev, NETMD_FFORWARD );
}

int netmd_rewind( netmd_dev_handle *dev )
{

	fprintf(stderr,"Starting rewind\n");
	return netmd_playback_control( dev, NETMD_REWIND );
}

int netmd_stop(netmd_dev_handle* dev)
{
	int ret = 0;
	char request[] = {0x00, 0x18, 0xc5, 0xff, 0x00, 0x00, 0x00, 0x00};  // Stop
	int size;
	char buf[255];

	fprintf(stderr,"Stopping minidisc \n");
	ret = netmd_exch_message(dev, request, 8, buf);

	if(ret < 0)
	{
		fprintf(stderr, "bad ret code, returning early\n");
		return 0;
	}

	size = ret;
	if (size<1) {
		fprintf(stderr, "Invalid size\n");
		return -1;
	}

	return 1;
}


int netmd_set_playmode(netmd_dev_handle* dev, int playmode)
{
	char request[] = {0x00, 0x18, 0xd1, 0xff, 0x01, 0x00,
                          0x00, 0x00, 0x88, 0x08, 0x00, 0x00,
                          0x00};
	char buf[255];
	int rsplen;

	request[10] = (playmode >> 8) & 0xFF;
	request[11] = (playmode >> 0) & 0xFF;
	rsplen = netmd_exch_message(dev, request, sizeof(request), buf);
	if (rsplen < 0) {
		printf("Error: netmd_exch_message failed %d\n", rsplen);
	}
	return rsplen;
}


int netmd_write_disc_header(netmd_dev_handle* devh, minidisc *md)
{
	int i;
	int dash = 0;
	int ret = 1;
	int header_size = 0;
	int tmp_size = 0;
	char* header = 0;
	char* tmp = 0;
	char* request = 0;
	char write_req[] = {0x00, 0x18, 0x07, 0x02, 0x20, 0x18, 0x01, 0x00, 0x00, 0x30, 0x00, 0x0a, 0x00, 0x50, 0x00, 0x00};
	char reply[255];

	/* calculate header length */
	for(i = 0; i < md->group_count; i++)
	{
		if(md->groups[i].start < 100)
		{
			if(md->groups[i].start < 10)
				header_size += 1;
			else
				header_size += 2;
		}
		else
			header_size += 3;

		if(md->groups[i].finish != 0)
		{
			if(md->groups[i].start < 100)
			{
				if(md->groups[i].start < 10)
					header_size += 1;
				else
					header_size += 2;
			}
			else
				header_size += 3;

			header_size++; /* room for the - */
		}

		header_size += 3; /* room for the ; and // tokens */
			header_size += strlen(md->groups[i].name);
	}
	header_size++;

	/* 	printf("New header length - %i (%02x)\n", header_size, header_size); */

	header = malloc(sizeof(char) * header_size);
	memset(header, 0, header_size);

	/* now generate the actual header from each group's info */
	for(i = 0; i < md->group_count; i++)
	{
		dash = 0; /* no dash */
		tmp_size = 0;

		if(md->groups[i].start < 100)
		{
			if(md->groups[i].start < 10)
				tmp_size += 1;
			else
				tmp_size += 2;
		}
		else
			header_size += 3;

		if(md->groups[i].finish != 0)
		{
			if(md->groups[i].start < 100)
			{
				if(md->groups[i].start < 10)
					tmp_size += 1;
				else
					tmp_size += 2;
			}
			else
				tmp_size += 3;

			dash = 1;
			tmp_size++; /* room for the - */
		}

		tmp_size += strlen(md->groups[i].name) + 3; /* name length + ; + // + NULL */
			tmp = malloc((tmp_size + 2));
			memset(tmp, 0, tmp_size);

			/* if group starts at 0 and it isn't the disc name group */
			if(md->groups[i].start == 0 && i != 0)
				snprintf(tmp, tmp_size, ";%s//", md->groups[i].name);
			else if(dash)
				snprintf(tmp, tmp_size, "%i-%i;%s//", md->groups[i].start, md->groups[i].finish, md->groups[i].name);
			else
				snprintf(tmp, tmp_size, "%i;%s//", md->groups[i].start, md->groups[i].name);

			strcat(header, tmp);
			strcat(header, "/");

			free(tmp);
	}

	request = malloc(header_size + 21);
	memset(request, 0, header_size + 21);

	memcpy(request, write_req, 16);
	request[16] = (header_size - 1); /* new size - null */
	request[20] = md->header_length; /* old size */

	tmp = request + 21;
	memcpy(tmp, header, header_size);

	ret = netmd_exch_message(devh, request, (header_size + 20), reply);
	free(request);

	return ret;
}


int netmd_write_track(netmd_dev_handle* devh, char* szFile)
{
        int ret = 0;
	int fd = open(szFile, O_RDONLY); /* File descriptor to omg file */
	char *data = malloc(4096); /* Buffer for reading the omg file */
	char *p = NULL; /* Pointer to index into data */
	char track_number='\0'; /* Will store the track number of the recorded song */

	char begintitle[] = {0x00, 0x18, 0x08, 0x10, 0x18, 0x02, 0x03, 0x00}; /* Some unknown command being send before titling */

	char endrecord[] =  {0x00, 0x18, 0x08, 0x10, 0x18, 0x02, 0x00, 0x00};  /* Some unknown command being send after titling */
	char fintoc[] =     {0x00, 0x18, 0x00, 0x08, 0x00, 0x46, 0xf0, 0x03,
						 0x01, 0x03, 0x48, 0xff, 0x00, 0x10, 0x01, 0x00,
						 0x25, 0x8f, 0xbf, 0x09, 0xa2, 0x2f, 0x35, 0xa3,
						 0xdd}; /* Command to finish toc flashing */
	char movetoendstartrecord[] = {0x00, 0x18, 0x00, 0x08, 0x00, 0x46, 0xf0, 0x03,
								   0x01, 0x03, 0x28, 0xff, 0x00, 0x01, 0x00, 0x10,
								   0x01, 0xff, 0xff, 0x00, 0x94, 0x02, 0x00, 0x00,
								   0x00, 0x06, 0x00, 0x00, 0x04, 0x98};   /* Record command */
	unsigned char movetoendresp[] = {0x0f, 0x18, 0x00, 0x08, 0x00, 0x46, 0xf0, 0x03,
							0x01, 0x03, 0x28, 0x00, 0x00, 0x01, 0x00, 0x10,
							0x01, 0x00, 0x11, 0x00, 0x94, 0x02, 0x00, 0x00,
							0x43, 0x8c, 0x00, 0x32, 0xbc, 0x50}; /* The expected response from the record command. */
	char header[] =      {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x3f, 0x00,
						  0xd4, 0x4b, 0xdc, 0xaa, 0xef, 0x68, 0x22, 0xe2}; /* Header to be inserted at every 0x3F10 bytes */
	//	char debug[]  =      {0x4c, 0x63, 0xa0, 0x20, 0x82, 0xae, 0xab, 0xa1};
	char size_request[4];
	int data_size_i; /* the size of the data part, later it will be used to point out the last byte in file */
	unsigned int size;
	char* buf=NULL; /* A buffer for recieving file info */
	usb_dev_handle	*dev;
	
	dev = (usb_dev_handle *)devh;
	
	if(fd < 0)
		return fd;

	if(!data)
		return ENOMEM;


	/********** Get the size of file ***********/
	lseek(fd, 0x56, SEEK_SET); // Read to data size
	read(fd,data,4);
	data_size_i =0;
	data_size_i = data[3];
	data_size_i += data[2] << 8;
	data_size_i += data[1] << 16;
	data_size_i += data[0] << 24;

	fprintf(stderr,"Size of data: %d\n",data_size_i);
	size = (data_size_i/0x3f18)*8+data_size_i+8;           // plus number of data headers
	fprintf(stderr,"Size of data w/ headers: %d\n",size);


	/********** Fill in information in start record command and send ***********/
	// not sure if size is 3 of 4 bytes in rec command...
	movetoendstartrecord[27]=(size >> 16) & 255;
	movetoendstartrecord[28]=(size >> 8) & 255;
	movetoendstartrecord[29]=size & 255;

	buf = sendcommand(devh, movetoendstartrecord, 30, movetoendresp, 0x1e);
	track_number = buf[0x12];


	/********** Prepare to send data ***********/
	lseek(fd, 90, SEEK_SET);  /* seek to 8 bytes before leading 8 00's */
	data_size_i += 90;        /* data_size_i will now contain position of last byte to be send */

	waitforsync(dev);   /* Wait for 00 00 00 00 from unit.. */


	/********** Send data ***********/
	while(ret >= 0)
	{
	          int file_pos=0;	/* Position in file */
		  int bytes_to_send;    /* The number of bytes that needs to be send in this round */

		  int __bytes_left;     /* How many bytes are left in the file */
		  int __chunk_size;     /* How many bytes are left in the 0x1000 chunk to send */
		  int __distance_to_header; /* How far till the next header insert */

		  file_pos = lseek(fd,0,SEEK_CUR); /* Gets the position in file, might be a nicer way of doing this */

		  fprintf(stderr,"pos: %d/%d; remain data: %d\n",file_pos,data_size_i,data_size_i-file_pos);
		  if (file_pos >= data_size_i) {
		    fprintf(stderr,"Done transfering\n");
		    free(data);
		    break;
		  }

		  __bytes_left = data_size_i-file_pos;
		  __chunk_size = min(0x1000,__bytes_left);
		  __distance_to_header = (file_pos-0x5a) % 0x3f10;
		  if (__distance_to_header !=0) __distance_to_header = 0x3f10 - __distance_to_header;
		  bytes_to_send = __chunk_size;

		  fprintf(stderr,"Chunksize: %d\n",__chunk_size);
		  fprintf(stderr,"distance_to_header: %d\n",__distance_to_header);
		  fprintf(stderr,"Bytes left: %d\n",__bytes_left);

		  if (__distance_to_header <= 0x1000) {   	     /* every 0x3f10 the header should be inserted, with an appropiate key.*/
			  fprintf(stderr,"Inserting header\n");

		          if (__chunk_size<0x1000) {                 /* if there is room for header then make room for it.. */
  			          __chunk_size += 0x10;              /* Expand __chunk_size */
				  bytes_to_send = __chunk_size-0x08; /* Expand bytes_to_send */
			  }

			  read(fd,data, __distance_to_header ); /* Errors checking should be added for read function */
			  __chunk_size -= __distance_to_header; /* Update chunk size */

			  p = data+__distance_to_header;        /* Change p to point at the position header should be inserted */
			  memcpy(p,header,16);
			  __bytes_left = min(0x3f00, __bytes_left-__distance_to_header-0x10);
			  fprintf (stderr, "bytes left in chunk: %d\n",__bytes_left);
			  p[6] = __bytes_left >> 8;      /* Inserts the higher 8 bytes of the length */
			  p[7] = __bytes_left & 255;     /* Inserts the lower 8 bytes of the length */
			  __chunk_size -= 0x10;          /* Update chunk size (for inserted header */

			  p += 0x10;                     /* p should now point at the beginning of the next data segment */
			  lseek(fd,8,SEEK_CUR);          /* Skip the key value in omg file.. Should probably be used for generating the header */
			  read(fd,p, __chunk_size);      /* Error checking should be added for read function */

		  } else {
 		          if(0 == read(fd, data, __chunk_size)) { /* read in next chunk */
			          free(data);
				  break;
			  }
		  }

		  netmd_trace(NETMD_TRACE_INFO, "Sending %d bytes to md\n", bytes_to_send);
		  netmd_trace_hex(NETMD_TRACE_INFO, data, min(0x4000, bytes_to_send));
		  ret = usb_bulk_write(dev,0x02, data, bytes_to_send, 5000);
	} /* End while */

	if (ret<0) {
	  free(data);
	  return ret;
	}


	/******** End transfer wait for unit ready ********/
	fprintf(stderr,"Waiting for Done:\n");
	do {
	        usb_control_msg(dev, 0xc1, 0x01, 0, 0, size_request, 0x04, 5000);
	} while  (memcmp(size_request,"\0\0\0\0",4)==0);

	netmd_trace(NETMD_TRACE_INFO, "Recieving response: \n");
	netmd_trace_hex(NETMD_TRACE_INFO, size_request, 4);
	size = size_request[2];
	if (size<1) {
	        fprintf(stderr, "Invalid size\n");
	        return -1;
	}
	buf = malloc(size);
	usb_control_msg(dev, 0xc1, 0x81, 0, 0, buf, size, 500);
	netmd_trace_hex(NETMD_TRACE_INFO, buf, size);
	free(buf);

	/******** Title the transfered song *******/
	buf = sendcommand(devh, begintitle, 8, NULL, 0);

	fprintf(stderr,"Renaming track %d to test\n",track_number);
	netmd_set_title(devh, track_number, "test");

	buf = sendcommand(devh, endrecord, 8, NULL, 0);


	/********* End TOC Edit **********/
	ret = usb_control_msg(dev, 0x41, 0x80, 0, 0, fintoc, 0x19, 800);

	fprintf(stderr,"Waiting for Done: \n");
	do {
	      usb_control_msg(dev, 0xc1, 0x01, 0, 0, size_request, 0x04, 5000);
	} while  (memcmp(size_request,"\0\0\0\0",4)==0);

	return ret;
}

int netmd_delete_track(netmd_dev_handle* dev, int track)
{
	int ret = 0;
	char request[] = {0x00, 0x18, 0x40, 0xff, 0x01, 0x00, 0x20, 0x10, 0x01, 0x00, 0x00};
	char reply[255];

	request[10] = track;
	ret = netmd_exch_message(dev, request, 11, reply);

	return ret;
}

int netmd_get_current_track(netmd_dev_handle* dev)
{
	int ret = 0;
	char request[] = {0x00, 0x18, 0x09, 0x80, 0x01, 0x04, 0x30, 0x88, 0x02, 0x00, 0x30, 0x88, 0x05, 0x00, 0x30, 0x00, 0x03, 0x00, 0x30, 0x00, 0x02, 0x00, 0xff, 0x00, 0x00, 0x00, 0x00, 0x00};

	int track = 0;

	char buf[255];

	ret = netmd_exch_message(dev, request, 28, buf);

	track = buf[36];

	return track;

}

float netmd_get_playback_position(netmd_dev_handle* dev)
{
	int ret = 0;
	char request[] = {0x00, 0x18, 0x09, 0x80, 0x01, 0x04, 0x30, 0x88, 0x02, 0x00, 0x30, 0x88, 0x05, 0x00, 0x30, 0x00, 0x03, 0x00, 0x30, 0x00, 0x02, 0x00, 0xff, 0x00, 0x00, 0x00, 0x00, 0x00};

	float position = 0.0f;

	int minutes = 0;
	int seconds = 0;
	int hundreds = 0;

	char buf[255];

	ret = netmd_exch_message(dev, request, 28, buf);

	minutes = BCD_TO_PROPER(buf[38]);
	seconds = BCD_TO_PROPER(buf[39]);
	hundreds = BCD_TO_PROPER(buf[40]);

	position = (minutes * 60) + seconds + ((float)hundreds / 100);

	if(position > 0)
	{
		return position;
	} else {
		return 0;
	}
}

void netmd_clean_disc_info(minidisc *md)
{
	int i = 0;
	for(; i < md->group_count; i++)
	{
		free(md->groups[i].name);
		md->groups[i].name = 0;
	}

	free(md->groups);

	md->groups = 0;
}

/*! testing function for testing weird unknown commands */
void test(netmd_dev_handle* devh)
{
  // 75 76 77 --- 7c
  // 7d
	/*   Unknown commands taken from log file.. Not necesary to start recording

	char unknown8[] = {0x00, 0x18, 0x08, 0x10, 0x10, 0x01, 0x01, 0x00};
	char response8[] = {0x09, 0x18, 0x08, 0x10, 0x10, 0x01, 0x01, 0x00};
	char unknown7[] = {0x00, 0x18, 0x06, 0x02, 0x10, 0x10, 0x01, 0x30, 0x00, 0x10, 0x00, 0xff, 0x00, 0x00, 0x00, 0x00, 0x00}; // probably disc info... with 0x11 tracks
	char response7[] = {0x09, 0x18, 0x06, 0x02, 0x10, 0x10, 0x01, 0x30, 0x00, 0x10, 0x00, 0x10, 0x00, 0x00, 0x08, 0x00, 0x00, 0x00, 0x06, 0x00, 0x10, 0x00, 0x02, 0x00, 0x11};
	char unknown6[] = {0x00, 0x18, 0x08, 0x10, 0x10, 0x01, 0x00, 0x00};
	char response6[] = {0x09, 0x18, 0x08, 0x10, 0x10, 0x01, 0x00, 0x00};
	char unknown5[] = {0x00, 0x18, 0x00, 0x08, 0x00, 0x46, 0xf0, 0x03, 0x01, 0x03, 0x80, 0xff};
	char response5[] = {0x09, 0x18, 0x00, 0x08, 0x00, 0x46, 0xf0, 0x03, 0x01, 0x03, 0x80, 0x00};
	char unknown4[] = {0x00,0x18, 0x00, 0x08, 0x0, 0x46, 0xf0, 0x03, 0x01, 0x03, 0x11, 0xff};
	char response4[] = {0x09, 0x18, 0x00, 0x08, 0x00, 0x46, 0xf0, 0x03, 0x01, 0x03, 0x11, 0x00, 0x01, 0x00, 0x00, 0x0a, 0xbc, 0xa8, 0x00, 0x00};
	char unknown3[] = {0x00, 0x18, 0x00, 0x08, 0x00, 0x46, 0xf0, 0x03, 0x01, 0x03, 0x12, 0xff, 0x00, 0x38, 0x00, 0x00, 0x00, 0x38, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x09, 0x00, 0x01, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00, 0x01, 0xca, 0xbe, 0x07, 0x2c, 0x4d, 0xa7, 0xae, 0xf3, 0x6c, 0x8d, 0x73, 0xfa, 0x60, 0x2b, 0xd1, 0x0f, 0xf4, 0x7d, 0x45, 0x9c, 0x72, 0xda, 0x81, 0x85, 0x16, 0x9d, 0x73, 0x49, 0x00, 0xff, 0x6c, 0x6a, 0xb9, 0x61, 0x6b, 0x03, 0x04, 0xf9, 0xce};
	char response3[] = {0x09, 0x18, 0x00, 0x08, 0x00, 0x46, 0xf0, 0x03, 0x01, 0x03, 0x12, 0x01, 0x00, 0x38, 0x00, 0x00, 0x00, 0x38};
	char unknown2[] = {0x00, 0x18, 0x00, 0x08, 0x00, 0x46, 0xf0, 0x03, 0x01, 0x03, 0x20, 0xff, 0x00, 0x00, 0x00, 0x42, 0x49, 0x5c, 0x1d, 0x43, 0x86, 0xf0, 0x97};
	char response2[] = {0x09, 0x18, 0x00, 0x08, 0x00, 0x46, 0xf0, 0x03, 0x01, 0x03, 0x20, 0x00, 0x00, 0x00, 0x00, 0x5b, 0x31, 0x10, 0x80, 0x55, 0x31, 0x5e, 0x87};
	char unknown1[] = {0x00, 0x18, 0x00, 0x08, 0x00, 0x46, 0xf0, 0x03, 0x01, 0x03, 0x22, 0xff, 0x00, 0x00, 0xb5, 0x51, 0x53, 0x9a, 0xd2, 0xca, 0x47, 0x35, 0x37, 0x86, 0xe1, 0xf2, 0xa8, 0xa3, 0x36, 0xea, 0x83, 0x9d, 0xe1, 0x51, 0xae, 0x58, 0xd7, 0x3b, 0x22, 0x2e, 0xaf, 0x98, 0x6e, 0xb1, 0xa0, 0x2a };
	*/
	//      Unnecesary commands for recording..
	//	ret = sendcommand(dev,unknown8,0x08,response8,0x08);
	//	ret = sendcommand(dev,unknown7,0x11,response7,0x19);
	//	ret = sendcommand(dev,unknown6,0x08,response6,0x08);
	//	ret = sendcommand(dev,unknown5,0x0c,response5,0x0c);
	//	ret = sendcommand(dev,unknown4,0x0c,response4,0x14);
	//	ret = sendcommand(dev,unknown3,0x4a,response3,0x12);
	//	ret = sendcommand(dev,unknown2,0x17,response2,0x17);
	//	ret = sendcommand(dev,unknown1,46,NULL,0);
	// 04 80 f3 9e 3b 16 5e 83 63 6e --- all zeros
	// 04 80 f3 9e 3b 16 5e 83 63 6e
	//unsigned char request[] = {0x00, 0x18, 0xc3, 0xFF, 0xFF, 0x00, 0x00, 0x00};                   // Play
  char movetoendstartrecord[] = {0x00, 0x18, 0x00, 0x08, 0x00, 0x46, 0xf0, 0x03,
				 0x01, 0x03, 0x28, 0xff, 0x00, 0x01, 0x00, 0x10,
				 0x01, 0xff, 0xff, 0x00, 0x94, 0x02, 0x00, 0x00, // mm= mode {a8 00=lp4, 94 02=lp2, 00 06=sp}
				 //      xx    xx    xx    mm   mm    xx    xx   // ll= length {largest size=0xac for both lp2,lp4 (-14.52min}
				 // -1.05
			  	   0x43, 0x8c, 0x00, 0x32, 0xbc, 0x50};   // 30 Move to end and start record;
  //                               0x05, 0x40  0x00  0x01  0xf8  0xc0   lp4
  //                               0x05, 0x56  0x00  0x2a  0xb0  0x18   sp
  //                               0x05  0x56  0x00  0x04  0x02  0x18   lp2 15secs
  //                               0x03, 0x78  0x00  0x02  0x9b  0x08   lp2 10secs
  //                               0x03, 0x88  0x00  0x02  0xa7  0x08   lp2 10secs
  //                               0x00, 0x06, 0x00  0x00  0x04  0x98
	//char rec_simple[] = {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	//					   0x00, 0x00, 0x28, 0x00, 0x00, 0x00, 0x00, 0x00,
	//					   0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	//					   0x00, 0x00, 0x00, 0x00, 0x00, 0x00};   // 30 Move to end and start record;
  unsigned char movetoendresp[] = {0x0f, 0x18, 0x00, 0x08, 0x00, 0x46, 0xf0, 0x03,
						  0x01, 0x03, 0x28, 0x00, 0x00, 0x01, 0x00, 0x10,
						  0x01, 0x00, 0x11, 0x00, 0x94, 0x02, 0x00, 0x00,
						  0x43, 0x8c, 0x00, 0x32, 0xbc, 0x50};
  char* ret=NULL;
  
  ret = sendcommand(devh, movetoendstartrecord, 30, movetoendresp, 30);
}
