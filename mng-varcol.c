#include <math.h>

static int isleft(Client *c);

/*
	Variable Column Layout

	- Special 'left' column, with helper to move clients in or out of the left
	column. This is useful for things like chat, or status monitor windows that
	you want to keep visible.

	- Variable number of master columns
	- Variable number of right columns
	- Variable number of master windows

	// TODO Calculate remainders
*/

/* The relative factors for the size of each column */
static const float colfact[3]			= { 0.1, 0.6, 0.3 };

/*
	Move a client within a column

	w		The width of the column
	x		The left position of the column
	offset	The offset of this client within the column
	count	The number of clients in the column
*/
static void placeClientInColumn(Monitor *m, Client *c, int w, int x, int offset, int count)
{
	struct wlr_box geom = {0};

	geom.width = w;
	geom.height = floor(m->w.height / count);

	geom.x = x;
	geom.y = m->w.y + (offset * geom.height);
	
	resize(c, geom, False);
}

/*
	variable column layout

	This layout has a variable number of columns, in 3 categories.
		0-1 small left columns, containing clients that have been "pushed" left
		1-n master columns
		0-n right columns
*/
void varcol(Monitor *m)
{
	int				masterw, leftw, rightw, x;
	unsigned int	i, leftn, rightn, mastern, coln, offset;
	float			colfacts;
	Client			*c, *tmp;
	int				nmastercols		= m->nmastercols;
	int				nrightcols		= m->nrightcols;
	struct wl_list	left_clients;

	/*
		Remove each of window that belongs in the left column, so they can be
		reattached at the end of the list below.
	*/
	wl_list_init(&left_clients);

	i = 0;
	wl_list_for_each_safe(c, tmp, &clients, link) {
		if (!VISIBLEON(c, m) || c->isfloating || c->isfullscreen) {
			continue;
		}

		if (i < m->nmaster) {
			/* Master */
			;
		} else if (isleft(c)) {
			/* Left; Detach and put in the left list */
			wl_list_remove(&c->link);
			wl_list_insert(&left_clients, &c->link);
		}

		i++;
	}

	/* Reattach the left clients to the main list */
	wl_list_for_each_reverse_safe(c, tmp, &left_clients, link) {
		wl_list_remove(&c->link);
		wl_list_insert(clients.prev, &c->link);
	}

	/* Count the windows for each column type */
	leftn = rightn = mastern = 0;
	wl_list_for_each(c, &clients, link) {
		if (!VISIBLEON(c, m) || c->isfloating || c->isfullscreen) {
			continue;
		}

		if (mastern < m->nmaster) {
			mastern++;
		} else if (isleft(c)) {
			leftn++;
		} else {
			rightn++;
		}
	}

	nmastercols	= MAX(MIN(mastern, nmastercols), 1);
	nrightcols	= MAX(MIN(rightn, nrightcols), 1);

	if (mastern == 0) {
		return;
	}

	/* Calculate the total colfacts value */
	colfacts = 0;

	/* Left column */
	if (leftn > 0) {
		colfacts += m->colfact[0];
	}

	/* Center column(s) */
	for (i = 0; i < nmastercols; i++) {
		colfacts += m->colfact[1];
	}

	/* Right column(s) */
	if (rightn > 0) {
		for (i = 0; i < nrightcols; i++) {
			colfacts += m->colfact[2];
		}
	}

	/* Calculate the width for each column type */
	leftw	= (m->w.width / colfacts) * m->colfact[0];
	masterw	= (m->w.width / colfacts) * m->colfact[1];
	rightw	= (m->w.width / colfacts) * m->colfact[2];

	/* Adjust right and left column to fit all clients */
	wl_list_for_each(c, &clients, link) {
		struct wlr_box min = {0}, max = {0};

		if (!VISIBLEON(c, m) || c->isfloating || c->isfullscreen) {
			continue;
		}

		/* Get client size hints */
		client_get_size_hints(c, &max, &min);

		if (i < mastern) {
			/* Master columns */

		} else if (!isleft(c)) {
			/* Right columns */
			x = min.width - rightw;
			if (x > 0) {
				rightw += x;
				masterw -= x;
			}
		} else if (leftn > 0) {
			/* left column */
			x = min.width - leftw;
			if (x > 0) {
				leftw += x;
				masterw -= x;
			}
		}
	}

	/* Place each client */
	x = m->w.x;
	if (leftn > 0) {
		x += leftw;
	}

	i = 0;
	wl_list_for_each(c, &clients, link) {
		struct wlr_box min = {0}, max = {0};

		if (!VISIBLEON(c, m) || c->isfloating || c->isfullscreen) {
			continue;
		}

		/* Get client size hints */
		client_get_size_hints(c, &max, &min);

		if (i < mastern) {
			/* Master columns */

			/* Offset within the section */
			offset = i;

			/* Max number of items in each master column */
			coln = ceil((float) mastern / nmastercols);

			placeClientInColumn(m, c, masterw, x, offset % coln, coln);

			/* Only increment x if this is the last client in this column */
			if ((++offset % coln) == 0) {
				x += masterw;
			}
		} else if (!isleft(c)) {
			/* Right columns */

			/* Offset within the section */
			offset = (i - mastern);

			/* Max number of items in each right column */
			coln = ceil((float) rightn / nrightcols);

			placeClientInColumn(m, c, rightw, x, offset % coln, coln);

			/* Only increment x if this is the last client in this column */
			if ((++offset % coln) == 0) {
				x += rightw;
			}
		} else if (leftn > 0) {
			/* left column */
			x = m->w.x;

			/* Offset within the section */
			offset = i - (mastern + rightn);

			/* There is only one left column */
			coln = leftn;

			placeClientInColumn(m, c, leftw, x, offset, leftn);
		}

		i++;
	}
}

static int isleft(Client *c)
{
	if (c == NULL) {
		return 0;
	}

	if (c->mon != NULL && c->mon->m.width <= 2000) {
		/* The left column is not worth using on a small monitor */
		return 0;
	}

	return c->isLeft;
}

/* Return non-zero if the currently selected client is in a master column */
static int ismaster(void)
{
	Client *c, *sel = focustop(selmon);
	int i;

	if (!sel || !selmon || !selmon->lt[selmon->sellt]->arrange || sel->isfloating) {
		return 0;
	}

	i = 0;
	wl_list_for_each(c, &clients, link) {
		if (!VISIBLEON(c, selmon) || c->isfloating || c->isfullscreen) {
			continue;
		}

		if (sel == c) {
			/* c is the selected client, and is index i */
			if (i < selmon->nmaster) {
				return 1;
			} else {
				return 0;
			}
		}
		i++;
	}

	return 0;
}

/* A value >= 1.0 sets that colfact to that value - 1.0 */
void setcolfact(const Arg *arg)
{
	Client	*sel = focustop(selmon);
	int		index = 1;

	if (!sel || !selmon || !selmon->lt[selmon->sellt]->arrange || sel->isfloating) {
		return;
	}

	if (ismaster()) {
		index = 0;
		/* master */
		index = 0;
	} else if (isleft(sel)) {
		/* left */
		index = -1;
	} else {
		/* right */
		index = 1;
	}
	index++;

	if (arg->f >= 1.0) {
		selmon->colfact[index] = arg->f - 1.0;
	} else {
		/* Adjust the argument based on the selected column */
		selmon->colfact[index] += arg->f;
	}

	if (selmon->colfact[index] < 0.1) {
		selmon->colfact[index] = 0.1;
	} else if (selmon->colfact[index] > 0.9) {
		selmon->colfact[index] = 0.9;
	}
	arrange(selmon);
}

static void pushleft(const Arg *arg)
{
	Client	*sel = focustop(selmon);

	if (!sel || !selmon || !selmon->lt[selmon->sellt]->arrange || sel->isfloating) {
		return;
	}

	sel->isLeft = !sel->isLeft;
	focusclient(sel, 1);
	arrange(selmon);
}

/*
	Modify either the right or master column count
*/
void incncols(const Arg *arg)
{
	Client	*sel = focustop(selmon);

	if (!sel || !selmon || !selmon->lt[selmon->sellt]->arrange || sel->isfloating) {
		return;
	}

	if (selmon->nmastercols < 1) selmon->nmastercols = 1;
	if (selmon->nrightcols < 1) selmon->nrightcols = 1;

	if (ismaster()) {
		/* master */
		selmon->nmastercols += arg->i;

		/* Auto adjust nmaster as well */
		selmon->nmaster = selmon->nmastercols;
	} else if (isleft(sel)) {
		/* left */
		;
	} else {
		/* right */
		selmon->nrightcols += arg->i;
	}

	if (selmon->nmastercols < 1) selmon->nmastercols = 1;
	if (selmon->nrightcols < 1) selmon->nrightcols = 1;

	arrange(selmon);
}

