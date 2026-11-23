/* Dead-key and Compose sequence state machine.
 *
 * Uses the compiled-in tl_compose_rules[] table generated from
 * layouts/compose. Both dead keys (starting with TL_KEY_DEAD_BIT | id)
 * and Compose sequences (starting with TL_COMPOSE_START) share this
 * single prefix-matching state machine. */
#include "tam-last/keys.h"

#include <string.h>

void tl_compose_init(struct tl_compose *c)
{
	memset(c, 0, sizeof(*c));
}

void tl_compose_reset(struct tl_compose *c)
{
	c->len = 0;
	c->active = false;
}

bool tl_compose_active(const struct tl_compose *c)
{
	return c->active;
}

static bool rule_matches_prefix(const struct tl_compose_rule *r,
	const uint32_t *seq, uint8_t len)
{
	if (r->len < len)
		return false;
	for (uint8_t i = 0; i < len; i++) {
		if (r->seq[i] != seq[i])
			return false;
	}
	return true;
}

enum tl_compose_status tl_compose_feed(struct tl_compose *c, uint32_t sym,
	uint32_t *out_cp)
{
	if (out_cp != NULL)
		*out_cp = 0;
	if (sym == 0)
		return c->active ? TL_COMPOSE_PENDING : TL_COMPOSE_IDLE;

	if (!c->active) {
		if ((sym & TL_KEY_DEAD_BIT) == 0)
			return TL_COMPOSE_IDLE;
		c->seq[0] = sym;
		c->len = 1;
		c->active = true;
		return TL_COMPOSE_PENDING;
	}

	/* Pressing Compose again restarts a fresh Compose sequence. */
	if (sym == TL_COMPOSE_START) {
		c->seq[0] = TL_COMPOSE_START;
		c->len = 1;
		c->active = true;
		return TL_COMPOSE_PENDING;
	}

	if (c->len >= TL_COMPOSE_MAX) {
		tl_compose_reset(c);
		return TL_COMPOSE_CANCELLED;
	}

	c->seq[c->len++] = sym;

	bool has_prefix = false;
	for (size_t i = 0; i < tl_compose_rule_count_val; i++) {
		const struct tl_compose_rule *r = &tl_compose_rules[i];
		if (!rule_matches_prefix(r, c->seq, c->len))
			continue;
		if (r->len == c->len) {
			if (out_cp != NULL)
				*out_cp = r->result;
			tl_compose_reset(c);
			return TL_COMPOSE_DONE;
		}
		has_prefix = true;
	}

	if (has_prefix)
		return TL_COMPOSE_PENDING;

	tl_compose_reset(c);
	return TL_COMPOSE_CANCELLED;
}
