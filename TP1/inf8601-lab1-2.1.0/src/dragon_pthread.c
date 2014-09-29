/*
 * dragon_pthread.c
 *
 *  Created on: 2011-08-17
 *      Author: Francis Giraldeau <francis.giraldeau@gmail.com>
 */

#define _GNU_SOURCE
#include <stdlib.h>
#include <pthread.h>
#include <stdarg.h>
#include <string.h>

#include "dragon.h"
#include "color.h"
#include "dragon_pthread.h"

pthread_mutex_t mutex_stdout;

void printf_safe(char *format, ...)
{
	va_list ap;

	va_start(ap, format);
	pthread_mutex_lock(&mutex_stdout);
	vprintf(format, ap);
	pthread_mutex_unlock(&mutex_stdout);
	va_end(ap);
}

void *dragon_draw_worker(void *data)
{
	struct draw_data *drw = (struct draw_data *) data;
	//printf("thread id is : %u \n",drw->id );
	/* 1. Initialiser la surface */
	int dragon_surface = drw->dragon_width * drw->dragon_height;
	int start1 = (uint64_t)(drw->id*dragon_surface/(double)drw->nb_thread);
	int end1 = (uint64_t)((drw->id+1)*dragon_surface/(double)drw->nb_thread)-1;
	//printf("interval1: %u \n", dragon_surface);	
	//printf("start1: %u \n",start1);
	//printf("end1: %u \n",end1);
	init_canvas(start1, end1, drw->dragon,-1);

	/* 2. Dessiner le dragon */
	uint64_t start = (uint64_t)(drw->id * drw->size / (double)drw->nb_thread);
	uint64_t end = (uint64_t)((drw->id + 1) * drw->size / (double)drw->nb_thread)-1;
	//printf("thread id is : %u \n",drw->id );
	//printf("interval2: %u \n", drw->size);	
	//printf("start2: %u \n",start);
	//printf("end2: %u \n",end);
	dragon_draw_raw(start, end, drw->dragon, drw->dragon_width, drw->dragon_height, drw->limits, drw->id);
	
	/* 3. Effectuer le rendu final */
	//printf("thread id is : %u \n",drw->id );
	uint64_t start3 = (uint64_t)(drw->id * drw->image_height / (double)drw->nb_thread);
	uint64_t end3 = (uint64_t)((drw->id + 1) * drw->image_height / (double)drw->nb_thread)-1;
	/*printf("interval3: %u \n", drw->image_height);	
	printf("start3: %u \n",start3);
	printf("end3: %u \n",end3);*/
	scale_dragon(start3, end3, drw->image, drw->image_width, drw->image_height, drw->dragon, drw->dragon_width, drw->dragon_height, drw->palette);
	return NULL;
}

int dragon_draw_pthread(char **canvas, struct rgb *image, int width, int height, uint64_t size, int nb_thread)
{
	pthread_t *threads = NULL;
	pthread_barrier_t barrier;
	limits_t limits;
	struct draw_data info;
	char *dragon = NULL;
	int scale_x;
	int scale_y;
	struct draw_data *data = NULL;
	struct palette *palette = NULL;
	int ret = 0;

	palette = init_palette(nb_thread);
	if (palette == NULL)
		goto err;

	if (pthread_barrier_init(&barrier, NULL, nb_thread) != 0) {
		printf("barrier init error\n");
		goto err;
	}

	/* 1. Calculer les limites du dragon */
	if (dragon_limits_pthread(&limits, size, nb_thread) < 0)
		goto err;

	info.dragon_width = limits.maximums.x - limits.minimums.x;
	info.dragon_height = limits.maximums.y - limits.minimums.y;

	if ((dragon = (char *) malloc(info.dragon_width * info.dragon_height)) == NULL) {
		printf("malloc error dragon\n");
		goto err;
	}

	if ((data = malloc(sizeof(struct draw_data) * nb_thread)) == NULL) {
		printf("malloc error data\n");
		goto err;
	}

	if ((threads = malloc(sizeof(pthread_t) * nb_thread)) == NULL) {
		printf("malloc error threads\n");
		goto err;
	}

	info.image_height = height;
	info.image_width = width;
	scale_x = info.dragon_width / width + 1;
	scale_y = info.dragon_height / height + 1;
	info.scale = (scale_x > scale_y ? scale_x : scale_y);
	info.deltaJ = (info.scale * width - info.dragon_width) / 2;
	info.deltaI = (info.scale * height - info.dragon_height) / 2;
	info.nb_thread = nb_thread;
	info.dragon = dragon;
	info.image = image;
	info.size = size;
	info.limits = limits;
	info.barrier = &barrier;
	info.palette = palette;
	info.dragon = dragon;
	info.image = image;
	printf("dragon_width %i dragon_height %i\n", info.dragon_width, info.dragon_height);
	/* 2. Lancement du calcul parallèle principal avec draw_dragon_worker */
	int m = 0;
	for (m = 0; m < nb_thread; m++) 
	{
		data[m] = info;
		data[m].id = m;
		pthread_create(threads+m, NULL, dragon_draw_worker, data+m);
	}

	/* 3. Attendre la fin du traitement */

	if (pthread_barrier_destroy(&barrier) != 0) {
		printf("barrier destroy error\n");
		goto err;
	}

	for(m = 0; m < nb_thread; ++m)
	{
		pthread_join(threads[m],NULL);
	}

done:
	FREE(data);
	FREE(threads);
	free_palette(palette);
	*canvas = dragon;
	return ret;

err:
	FREE(dragon);
	ret = -1;
	goto done;
}

void *dragon_limit_worker(void *data)
{
	struct limit_data *lim = (struct limit_data *) data;
	piece_limit(lim->start, lim->end, &lim->piece);
	return NULL;
}

/*
 * Calcule les limites en terme de largeur et de hauteur de
 * la forme du dragon. Requis pour allouer la matrice de dessin.
 */
int dragon_limits_pthread(limits_t *limits, uint64_t size, int nb_thread)
{
	int ret = 0;
	pthread_t *threads = NULL;
	struct limit_data *thread_data = NULL;
	piece_t master;

	piece_init(&master);

	if ((threads = calloc(nb_thread, sizeof(pthread_t))) == NULL)
		goto err;

	if ((thread_data = calloc(nb_thread, sizeof(struct limit_data))) == NULL)
		goto err;
	
	/* 1. Lancement du calcul en parallèle avec dragon_limit_worker */
	printf("size: %u \n",size);
	int i = 0;
	for( i = 0; i < nb_thread; ++i)
	{
		thread_data[i].start = (uint64_t)(i*size/(double)nb_thread);
		thread_data[i].end = (uint64_t)((i+1)*size/(double)nb_thread)-1;
//		printf("start: %u \n",thread_data[i].start);
//		printf("end: %u \n",thread_data[i].end);
		thread_data[i].piece = master;
		pthread_create(threads+i, NULL, dragon_limit_worker, thread_data+i);
	}

	/* 2. Attendre la fin du traitement */
	for(i = 0; i < nb_thread; ++i)
	{
		pthread_join(threads[i],NULL);
	}

	/* 3. Fusion des pièces */
	for(i = 0; i < nb_thread ; ++i)
	{
		piece_merge(&(master),(thread_data[i].piece));
	}	
	
done:
	FREE(threads);
	FREE(thread_data);
	*limits = master.limits;
	return ret;
err:
	ret = -1;
	goto done;
}