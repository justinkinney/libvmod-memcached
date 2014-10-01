#include <libmemcached/memcached.h>

#include "vrt.h"
#include "bin/varnishd/cache.h"

#include "vcc_if.h"

/* A word about memory management
 *
 * In this VMOD, the per-VCL data is the 'memcached_server_st' structure,
 * accessed by functions with the 'priv vcl' option as priv->priv.
 *
 * Pthreads is used to associated a thread-specific value with each
 * thread, and there we store the 'memcached_st' structure. The
 * memcached_free function is registered as the destructor.
 **/

/** Initialize this module and thread-local data **/

typedef void (*thread_destructor)(void *);

static pthread_once_t thread_once = PTHREAD_ONCE_INIT;
static pthread_key_t thread_key;

static void
make_key()
{
	pthread_key_create(&thread_key, (thread_destructor)memcached_free);
}

memcached_st *
get_memcached(void *server_list)
{
	memcached_st *mc = pthread_getspecific(thread_key);
	if (!mc) {
#if defined(LIBMEMCACHED_VERSION_HEX) && LIBMEMCACHED_VERSION_HEX > 0x00049000
		if (strstr(server_list, "--SERVER")) {
			mc = memcached(server_list, strlen(server_list));
		} else {
#endif
			memcached_server_st *servers =
			    memcached_servers_parse(server_list);
			mc = memcached_create(NULL);
			if (mc == NULL) VSL(SLT_VCL_Log, 0,
			    "Allocation failure on memcached context.");
			memcached_server_push(mc,
			    (memcached_server_st *)servers);
			memcached_server_list_free(servers);
#if defined(LIBMEMCACHED_VERSION_HEX) && LIBMEMCACHED_VERSION_HEX > 0x00049000
		}
#endif
		pthread_setspecific(thread_key, mc);
	}
	return (mc);
}

int
init_function(struct vmod_priv *priv, const struct VCL_conf *conf)
{
	(void)conf;

	priv->free = (vmod_priv_free_f *)memcached_server_list_free;

	pthread_once(&thread_once, make_key);

	return (0);
}

/** The following may ONLY be called from VCL_init **/

void
vmod_servers(struct sess *sp, struct vmod_priv *priv, const char *config)
{
	(void)sp;

	priv->priv = (char *)config;
}

/** The following may be called after 'memcached.servers(...)' **/

void
vmod_set(struct sess *sp, struct vmod_priv *priv, const char *key, const char *value, int expiration, int flags)
{
	memcached_st *mc = get_memcached(priv->priv);

	(void)sp;

	if (mc)
		memcached_set(mc, key, strlen(key), value, strlen(value),
		    expiration, flags);
}

const char *
vmod_get(struct sess *sp, struct vmod_priv *priv, const char *key)
{
	size_t len;
	uint32_t flags;
	memcached_return rc;
	memcached_st *mc = get_memcached(priv->priv);
	char *p, *value;

	if (!mc)
		return (NULL);

	value = memcached_get(mc, key, strlen(key), &len, &flags, &rc);
	if (!value)
		return (NULL);

	p = WS_Dup(sp->ws, value);
	free(value);

	return (p);
}

int
vmod_incr(struct sess *sp, struct vmod_priv *priv, const char *key, int offset)
{
	uint64_t value = 0;
	memcached_st *mc = get_memcached(priv->priv);

	(void)sp;

	if (!mc)
		return (0);

	memcached_increment(mc, key, strlen(key), offset, &value);

	return ((int)value);
}

int
vmod_decr(struct sess *sp, struct vmod_priv *priv, const char *key, int offset)
{
	uint64_t value = 0;

	memcached_st *mc = get_memcached(priv->priv);
	(void)sp;

	if (!mc)
		return (0);

	memcached_decrement(mc, key, strlen(key), offset, &value);

	return ((int)value);
}

int
vmod_incr_set(struct sess *sp, struct vmod_priv *priv, const char *key,
    int offset, int initial, int expiration)
{
	uint64_t value = 0;

#if defined(LIBMEMCACHED_VERSION_HEX) && LIBMEMCACHED_VERSION_HEX < 0x00049000
	VSL(SLT_VCL_Log, 0, "memcached: Function unsupported by libmemcached");
	return(0);
#endif

	memcached_st *mc = get_memcached(priv->priv);

	(void)sp;

	if (!mc)
		return (0);

	memcached_increment_with_initial(mc, key, strlen(key), offset,
	    initial, expiration, &value);

	return ((int)value);
}

int
vmod_decr_set(struct sess *sp, struct vmod_priv *priv, const char *key,
    int offset, int initial, int expiration)
{
	uint64_t value = 0;

#if defined(LIBMEMCACHED_VERSION_HEX) && LIBMEMCACHED_VERSION_HEX < 0x00049000
	VSL(SLT_VCL_Log, 0, "memcached: Function unsupported by libmemcached");
	return(0);
#endif

	memcached_st *mc = get_memcached(priv->priv);

	(void)sp;

	if (!mc)
		return (0);

	memcached_decrement_with_initial(mc, key, strlen(key), offset,
	    initial, expiration, &value);

	return ((int)value);
}
