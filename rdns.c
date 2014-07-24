#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <stdlib.h>
#include <php.h>
#include "rdns.h"
#include "rdns_curve.h"
#include "rdns_ev.h"

#define PHP_RDNS_VERSION "0.1.0"
#define PHP_RDNS_EXTNAME "RDNS"

#define RDNS_INIT_VARS                          \
  zval *object = getThis();                     \
  php_rdns_t *i_obj = NULL;

#define RDNS_FETCH_OBJECT                                                  \
  i_obj = (php_rdns_t *) zend_object_store_get_object( object TSRMLS_CC );

typedef struct {
  zend_object obj;
  zval *result;
  struct ev_loop *loop;
  struct rdns_resolver *resolver;
  size_t nreq;
} php_rdns_t;

typedef struct {
  php_rdns_t *rdns;
  size_t index;
} php_rdns_context_t;

static zend_class_entry *rdns_ce = NULL;

extern zend_module_entry rdns_module_entry;
#define phpext_rdns_ptr &rdns_module_entry

static PHP_METHOD(RDNS, __construct);
static PHP_METHOD(RDNS, addServer);
static PHP_METHOD(RDNS, addRequest);
static PHP_METHOD(RDNS, getReplies);

ZEND_BEGIN_ARG_INFO_EX(arginfo_addServer, 0, 0, 1)
  ZEND_ARG_INFO(0, server)
  ZEND_ARG_INFO(0, port)
  ZEND_ARG_INFO(0, prio)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO_EX(arginfo_addRequest, 0, 0, 3)
  ZEND_ARG_INFO(0, hostname)
  ZEND_ARG_INFO(0, type)
  ZEND_ARG_INFO(0, timeout)
ZEND_END_ARG_INFO()

static zend_function_entry rdns_class_methods[] = {
  PHP_ME(RDNS, __construct, NULL, ZEND_ACC_PUBLIC | ZEND_ACC_CTOR)
  PHP_ME(RDNS, addServer,   arginfo_addServer, ZEND_ACC_PUBLIC)
  PHP_ME(RDNS, addRequest,  arginfo_addRequest, ZEND_ACC_PUBLIC)
  PHP_ME(RDNS, getReplies,  NULL, ZEND_ACC_PUBLIC)
  PHP_FE_END
};
/* }}} */

static void php_rdns_free(php_rdns_t *i_obj TSRMLS_DC)
{
  zend_object_std_dtor(&i_obj->obj TSRMLS_CC);
  rdns_resolver_release(i_obj->resolver);
  efree(i_obj);
}

static zend_object_value
php_rdns_new(zend_class_entry *ce TSRMLS_DC)
{
  zend_object_value retval;
  php_rdns_t *i_obj;
  zval *tmp;

  i_obj = ecalloc(1, sizeof(*i_obj));
  zend_object_std_init(&i_obj->obj, ce TSRMLS_CC );

  retval.handle = zend_objects_store_put(i_obj,
                                         (zend_objects_store_dtor_t)
                                         zend_objects_destroy_object,
                                         (zend_objects_free_object_storage_t)
                                         php_rdns_free, NULL TSRMLS_CC);
  retval.handlers = zend_get_std_object_handlers();

  return retval;
}

/* {{{ PHP_MINIT_FUNCTION */
static PHP_MINIT_FUNCTION(rdns)
{
  zend_class_entry ce;
  INIT_CLASS_ENTRY(ce, PHP_RDNS_EXTNAME, rdns_class_methods);
  rdns_ce = zend_register_internal_class(&ce TSRMLS_CC);
  rdns_ce->create_object = php_rdns_new;

#define RDNS_TYPE_CONST(TYPE)                               \
  REGISTER_LONG_CONSTANT("RDNS_"#TYPE, RDNS_REQUEST_##TYPE,  \
                         CONST_CS | CONST_PERSISTENT);
  RDNS_TYPE_CONST(A);
  RDNS_TYPE_CONST(NS);
  RDNS_TYPE_CONST(MX);
  RDNS_TYPE_CONST(TXT);
  RDNS_TYPE_CONST(SRV);
  RDNS_TYPE_CONST(AAAA);
#undef RDNS_TYPE_CONST

  return SUCCESS;
}
/* }}} */

/* {{{ PHP_MINIT_FUNCTION */
static PHP_MINFO_FUNCTION(rdns)
{
  php_info_print_table_start();
  php_info_print_table_header(2, "RDNS Support", "enabled");
  php_info_print_table_row(2, "RDNS Version", PHP_RDNS_VERSION);
  php_info_print_table_row(2, "RDNS GitHub", "http://github.com/weheartwebsites/php-rdns");
  php_info_print_table_row(2, "RDNS librdns", "08f7afd8f1716f1ac9ce7825f0f9bce1345bb48c");
  php_info_print_table_end();
}
/* }}} */

// the following code creates an entry for the module and registers it with Zend.
zend_module_entry rdns_module_entry = {
#if ZEND_MODULE_API_NO >= 20050922
  STANDARD_MODULE_HEADER_EX,
  NULL,
  NULL,
#else
  STANDARD_MODULE_HEADER,
#endif
  PHP_RDNS_EXTNAME,
  NULL,
  PHP_MINIT(rdns),
  NULL, // SHUTDOWN
  NULL, // name of the RINIT function or NULL if not applicable
  NULL, // name of the RSHUTDOWN function or NULL if not applicable
  PHP_MINFO(rdns),
  PHP_RDNS_VERSION,
  STANDARD_MODULE_PROPERTIES
};

#ifdef COMPILE_DL_RDNS
ZEND_GET_MODULE(rdns)
#endif

/* {{{ RDNS::__construct() */
static PHP_METHOD(RDNS, __construct)
{
  zval *object = getThis();
  php_rdns_t *i_obj = NULL;

  if (zend_parse_parameters(ZEND_NUM_ARGS() TSRMLS_CC, "") == FAILURE) {
    ZVALL_NULL(object);
    return;
  }

  i_obj = (php_rdns_t *) zend_object_store_get_object(object TSRMLS_CC);
  if (!(i_obj->resolver = rdns_resolver_new())) {
    php_error_docref(NULL TSRMLS_CC, E_ERROR, "could not create resolver structure");
  }

  i_obj->nreq = 0;
  i_obj->loop = ev_default_loop(0);
  rdns_bind_libev(i_obj->resolver, i_obj->loop);
  rdns_resolver_set_log_level (i_obj->resolver, RDNS_LOG_DEBUG);
  rdns_resolver_set_max_io_uses (i_obj->resolver, 1, 0.1);
}
/* }}} */


/* {{{ bool RDNS::addServer(string $server [, int $port = 53 [, int $prio = 0 ] ]) */
static PHP_METHOD(RDNS, addServer)
{
  char *server;
  size_t server_len;
  long port = 53, prio = 0;

  RDNS_INIT_VARS;

  if (zend_parse_parameters(ZEND_NUM_ARGS() TSRMLS_CC, "s|ll",
                            &server, &server_len, &port, &prio) == FAILURE) {
    return;
  }

  RDNS_FETCH_OBJECT;

  RETURN_BOOL(rdns_resolver_add_server(i_obj->resolver, server, port, prio, 8));
}
/* }}} */

static void
rdns_reply_callback(struct rdns_reply *reply, void *arg)
{
  struct rdns_reply_entry *entry;
  char ipstr[INET6_ADDRSTRLEN + 1];
  zval *result, *result_item;
  php_rdns_context_t *context = (php_rdns_context_t *) arg;
  struct rdns_request_name *name = rdns_request_get_name(reply->request, NULL);

  MAKE_STD_ZVAL(result);
  array_init(result);
  if (reply->code == RDNS_RC_NOERROR) {
    entry = reply->entries;
    while (entry != NULL) {
      MAKE_STD_ZVAL(result_item);
      array_init(result_item);
      add_assoc_string_ex(result_item, ZEND_STRS("host"),
                          name->name, 1);
      add_assoc_string_ex(result_item, ZEND_STRS("class"), "IN", 1);
      add_assoc_long(result_item, "ttl", entry->ttl);

      if (entry->type == RDNS_REQUEST_A) {
        add_assoc_string_ex(result_item, ZEND_STRS("type"), "A", 1);
        inet_ntop (AF_INET, &entry->content.a.addr, ipstr, sizeof (ipstr));
        add_assoc_string_ex(result_item, ZEND_STRS("ip"), ipstr, 1);
      } else if (entry->type == RDNS_REQUEST_AAAA) {
        add_assoc_string_ex(result_item, ZEND_STRS("type"), "AAAA", 1);
        inet_ntop (AF_INET6, &entry->content.aaa.addr, ipstr, sizeof ipstr);
        add_assoc_string_ex(result_item, ZEND_STRS("ipv6"), ipstr, 1);
      } else if (entry->type == RDNS_REQUEST_MX) {
        add_assoc_string_ex(result_item, ZEND_STRS("type"), "MX", 1);
        add_assoc_long(result_item, "pri", entry->content.mx.priority);
        add_assoc_string_ex(result_item, ZEND_STRS("target"),
                            entry->content.mx.name, 1);
      } else if (entry->type == RDNS_REQUEST_NS) {
        add_assoc_string_ex(result_item, ZEND_STRS("type"), "NS", 1);
        add_assoc_long(result_item, "pri", entry->content.mx.priority);
        add_assoc_string_ex(result_item, ZEND_STRS("target"),
                            entry->content.mx.name, 1);
      } else if (entry->type == RDNS_REQUEST_TXT) {
        add_assoc_string_ex(result_item, ZEND_STRS("type"), "TXT", 1);
        add_assoc_string_ex(result_item, ZEND_STRS("txt"),
                            entry->content.txt.data, 1);
      } else if (entry->type == RDNS_REQUEST_SRV) {
        add_assoc_string_ex(result_item, ZEND_STRS("type"), "SRV", 1);
        add_assoc_string_ex(result_item, ZEND_STRS("target"),
                            entry->content.srv.target, 1);
        add_assoc_long(result_item, "pri", entry->content.srv.priority);
        add_assoc_long(result_item, "weight", entry->content.srv.weight);
        add_assoc_long(result_item, "port", entry->content.srv.port);
      }
      entry = entry->next;
      add_next_index_zval(result, result_item);
    }
  }
  add_index_zval(context->rdns->result, context->index, result);

  if (!--context->rdns->nreq) {
    rdns_resolver_release(context->rdns->resolver);
  }
}

/* {{{ bool RDNS::addRequest(string $hostname, int $type, float $timeout) */
static PHP_METHOD(RDNS, addRequest)
{
  char *hostname;
  size_t hostname_len;
  long type;
  double timeout;
  php_rdns_context_t *context;

  RDNS_INIT_VARS;

  if (zend_parse_parameters(ZEND_NUM_ARGS() TSRMLS_CC,
                            "sld",
                            &hostname,
                            &hostname_len,
                            &type,
                            &timeout) == FAILURE) {
    return;
  }

  RDNS_FETCH_OBJECT;

  context = (php_rdns_context_t *) malloc(sizeof *context);
  context->rdns = i_obj;
  context->index = i_obj->nreq++;
  if (!context->index) {
    rdns_resolver_init(i_obj->resolver);
  }
  RETURN_BOOL(rdns_make_request_full(i_obj->resolver, rdns_reply_callback,
                                     context, timeout, 1, 1,
                                     hostname, type));
}
/* }}} */


/* {{{ array RDNS::getReplies() */
static PHP_METHOD(RDNS, getReplies)
{
  RDNS_INIT_VARS;

  if (zend_parse_parameters(ZEND_NUM_ARGS() TSRMLS_CC, "") == FAILURE) {
    return;
  }

  RDNS_FETCH_OBJECT;

  array_init(return_value);
  i_obj->result = return_value;
  if (i_obj->nreq) {
    ev_run(i_obj->loop, 0);
    /* ev_loop_destroy(i_obj->loop); */
  }
}
/* }}} */
