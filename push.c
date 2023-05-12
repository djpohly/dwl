static Client *
nexttiled(Client *sel) {
	Client *c;
	wl_list_for_each(c, &sel->link, link) {
		if (&c->link == &clients)
			break;  /* don't wrap */
		if (!c->isfloating && VISIBLEON(c, selmon))
			return c;
	}
	return NULL;
}

static Client *
prevtiled(Client *sel) {
	Client *c;
	wl_list_for_each_reverse(c, &sel->link, link) {
		if (&c->link == &clients)
			break;  /* don't wrap */
		if (!c->isfloating && VISIBLEON(c, selmon))
			return c;
	}
	return NULL;
}

static void
pushup(const Arg *arg) {
	Client *sel = focustop(selmon);
	Client *c;

	if(!sel || sel->isfloating)
		return;
	if((c = prevtiled(sel))) {
		/* attach before c */
		wl_list_remove(&sel->link);
		wl_list_insert(c->link.prev, &sel->link);
	} else {
		/* move to the end */
		wl_list_remove(&sel->link);
		wl_list_insert(clients.prev, &sel->link);
	}
	focusclient(sel, 1);
	arrange(selmon);
}

static void
pushdown(const Arg *arg) {
	Client *sel = focustop(selmon);
	Client *c;

	if(!sel || sel->isfloating)
		return;
	if((c = nexttiled(sel))) {
		/* attach after c */
		wl_list_remove(&sel->link);
		wl_list_insert(&c->link, &sel->link);
	} else {
		/* move to the front */
		wl_list_remove(&sel->link);
		wl_list_insert(&clients, &sel->link);
	}
	focusclient(sel, 1);
	arrange(selmon);
}
