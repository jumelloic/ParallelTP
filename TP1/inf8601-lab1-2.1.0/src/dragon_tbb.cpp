/*
 * dragon_tbb.c
 *
 *  Created on: 2011-08-17
 *      Author: Francis Giraldeau <francis.giraldeau@gmail.com>
 */

#include <iostream>

extern "C" {
#include "dragon.h"
#include "color.h"
#include "utils.h"
}
#include "dragon_tbb.h"
#include "tbb/tbb.h"
#include "TidMap.h"
#include <iostream>
using namespace std;
using namespace tbb;

static TidMap* tid = NULL;

class DragonLimits {
	public:
		DragonLimits()
		{
			piece_init(&value);
		}
		DragonLimits(const DragonLimits& drgL, split)
		{
			piece_init(&value);
		}
		void operator()(const blocked_range<int>& r)
		{
			piece_limit(r.begin(), r.end(), &value);
		}
		void join(DragonLimits& p)
		{
			piece_merge(&value, p.value);
		}
		piece_t& getPiece()
		{
			return value;
		}
		piece_t value;
};

class DragonDraw 
{
	public:
		DragonDraw(struct draw_data* parData)
		: data(parData)
		, index (0)
		{
	
		}
		DragonDraw(const DragonDraw& drgL)
		: data(drgL.data)
		{
			// L'index est laissé pseudo aléatoire pour donner des couleurs différentes 
			// Indexation de l'ID
			tid->getIdFromTid(gettid());
		}
		void operator()(const blocked_range<uint64_t>& r) const
		{
			dragon_draw_raw(r.begin(), r.end(), data->dragon, data->dragon_width, data->dragon_height, data->limits, index);
		}
		struct draw_data* data;
		int index;
};

class DragonRender 
{
	public:
		DragonRender(struct draw_data* parData)
		: data(parData)
		{
		}
		DragonRender(const DragonRender& drgR)
		: data(drgR.data)
		{
		}
		void operator()(const blocked_range<int>& r) const
		{
			scale_dragon(r.begin(), r.end(), data->image, data->image_width, data->image_height, data->dragon, data->dragon_width, data->dragon_height, data->palette);
		}
		struct draw_data* data;
};

class DragonClear {
public:
	DragonClear(char initVal, char *parCanvas)
	: value(initVal)
	, canvas(parCanvas)
	{
		
	}
	DragonClear(const DragonClear& drgC)
	: value(drgC.value)
	, canvas(drgC.canvas)
	{
	}	
	void operator()(const blocked_range<int>& r) const
	{
		init_canvas(r.begin(), r.end(), canvas, -1);
	}	
	char value;
	char *canvas;
};

int dragon_draw_tbb(char **canvas, struct rgb *image, int width, int height, uint64_t size, int nb_thread)
{
	// Allocation mémoire du TIDMAP
	tid = new TidMap(nb_thread*2);

	struct draw_data data;
	limits_t limits;
	char *dragon = NULL;
	int dragon_width;
	int dragon_height;
	int dragon_surface;
	int scale_x;
	int scale_y;
	int scale;
	int deltaJ;
	int deltaI;

	struct palette *palette = init_palette(nb_thread);
	if (palette == NULL)
		return -1;

	/* 1. Calculer les limites du dragon */
	dragon_limits_tbb(&limits, size, nb_thread);

	dragon_width = limits.maximums.x - limits.minimums.x;
	dragon_height = limits.maximums.y - limits.minimums.y;
	dragon_surface = dragon_width * dragon_height;
	scale_x = dragon_width / width + 1;
	scale_y = dragon_height / height + 1;
	scale = (scale_x > scale_y ? scale_x : scale_y);
	deltaJ = (scale * width - dragon_width) / 2;
	deltaI = (scale * height - dragon_height) / 2;

	dragon = (char *) malloc(dragon_surface);
	if (dragon == NULL) {
		free_palette(palette);
		*canvas = NULL;
		return -1;
	}

	data.nb_thread = nb_thread;
	data.dragon = dragon;
	data.image = image;
	data.size = size;
	data.image_height = height;
	data.image_width = width;
	data.dragon_width = dragon_width;
	data.dragon_height = dragon_height;
	data.limits = limits;
	data.scale = scale;
	data.deltaI = deltaI;
	data.deltaJ = deltaJ;
	data.palette = palette;

	task_scheduler_init init(nb_thread);

	/* 2. Initialiser la surface : DragonClear */
	DragonClear clear(-1, dragon);
	parallel_for(blocked_range<int>(0, dragon_surface), clear);
	/* 3. Dessiner le dragon : DragonDraw */
	// Draw dragon
	DragonDraw draw(&data);
	parallel_for(blocked_range<uint64_t>(0, data.size), draw);

	/* 4. Effectuer le rendu final : DragonRender */
	DragonRender render(&data);
	parallel_for(blocked_range<int>(0, height), render);
	
	init.terminate();
	free_palette(palette);
	*canvas = dragon;
	tid->dump();
	delete tid;
	return 0;
}

/*
 * Calcule les limites en terme de largeur et de hauteur de
 * la forme du dragon. Requis pour allouer la matrice de dessin.
 */
int dragon_limits_tbb(limits_t *limits, uint64_t size, int nb_thread)
{
	DragonLimits lim;
	task_scheduler_init task(nb_thread);

	parallel_reduce(blocked_range<int>(0, size), lim);
	
	piece_t piece = lim.getPiece();
	*limits = piece.limits;
	
	return 0;
}
