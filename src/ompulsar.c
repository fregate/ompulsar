#include <stdlib.h>

#include <rsyslog.h>

#include <errmsg.h>
#include <module-template.h>
#include <typedefs.h>

#include <hashmap.h>

#include <pulsar/c/client.h>
#include <pulsar/c/message.h>
#include <pulsar/c/producer.h>

#include "defines.h"

MODULE_TYPE_OUTPUT
MODULE_TYPE_NOKEEP
MODULE_CNFNAME("ompulsar");

/* internal structures
 */
DEF_OMOD_STATIC_DATA

// statsobj_t *pulsar_stats;
// STATSCOUNTER_DEF(stat_queue_size, mutCtrQueueSize);
// STATSCOUNTER_DEF(stat_topic_submit, mutCtrTopicSubmit);
// STATSCOUNTER_DEF(stat_KafkaFail, mutCtrKafkaFail);
// STATSCOUNTER_DEF(stat_CacheMiss, mutCtrCacheMiss);
// STATSCOUNTER_DEF(stat_CacheEvict, mutCtrCacheEvict);
// STATSCOUNTER_DEF(stat_CacheSkip, mutCtrCacheSkip);
// STATSCOUNTER_DEF(stat_KafkaAck, mutCtrKafkaAck);
// STATSCOUNTER_DEF(stat_KafkaMsgTooLarge, mutCtrKafkaMsgTooLarge);
// STATSCOUNTER_DEF(stat_KafkaUnknownTopic, mutCtrKafkaUnknownTopic);
// STATSCOUNTER_DEF(stat_KafkaQueueFull, mutCtrKafkaQueueFull);
// STATSCOUNTER_DEF(stat_KafkaUnknownPartition, mutCtrKafkaUnknownPartition);
// STATSCOUNTER_DEF(stat_KafkaOtherErrors, mutCtrKafkaOtherErrors);
// STATSCOUNTER_DEF(stat_KafkaRespTimedOut, mutCtrKafkaRespTimedOut);
// STATSCOUNTER_DEF(stat_KafkaRespTransport, mutCtrKafkaRespTransport);
// STATSCOUNTER_DEF(stat_KafkaRespBrokerDown, mutCtrKafkaRespBrokerDown);
// STATSCOUNTER_DEF(stat_KafkaRespAuth, mutCtrKafkaRespAuth);
// STATSCOUNTER_DEF(stat_KafkaRespOther, mutCtrKafkaRespOther);

struct producer_tuple
{
	char * topic_;
	pulsar_producer_t * producer_;
	pulsar_producer_configuration_t * producer_config_;
};

int producer_tuple_compare(const void * a, const void * b, void *)
{
	const struct producer_tuple * pa = a;
	const struct producer_tuple * pb = b;
	return strcmp(pa->topic_, pb->topic_);
}

uint64_t producer_tuple_hash(const void * item, uint64_t seed0, uint64_t seed1)
{
	const struct producer_tuple * p = item;
	return hashmap_murmur(p->topic_, strlen(p->topic_), seed0, seed1);
}

void producer_tuple_free(void * item)
{
	const struct producer_tuple * p = item;

	free(p->topic_);
	pulsar_producer_free(p->producer_);
	pulsar_producer_configuration_free(p->producer_config_);
}

typedef struct InstanceData_
{
	char * template_;  // todo is it necessary field?
	pulsar_producer_t * producer_;  // do not free it! it'll be freed at module destruction
} instanceData;

typedef struct WrkrInstanceData_
{
	instanceData * data;
} wrkrInstanceData_t;

// plugin settings
const char PARAM_ENDPOINT[] = "endpoint";
const char PARAM_CLIENT_MEMORY_LIMIT[] = "memory.limit";
// const char PARAM_CLIENT_AUTH[] = "authentication";
// const char PARAM_CLIENT_AUTH_PARAM[] = "auth.param";
const char PARAM_CLIENT_OPERATION_TIMEOUT[] = "operation.timeout";
const char PARAM_CLIENT_IO_THREADS[] = "io.threads";
const char PARAM_CLIENT_LISTENER_THREADS[] = "message.listener.threads";
const char PARAM_CLIENT_LOOKUP_REQUESTS[] = "lookup.requests";
const char PARAM_CLIENT_USE_TLS[] = "tls";
const char PARAM_CLIENT_TLS_TRUST_CERT[] = "tls.trust.certs.file";
const char PARAM_CLIENT_TLS_ALLOW_INSECURE_CONN[] = "tls.allow.insecure.connection";
const char PARAM_CLIENT_VALIDATE_HOST_NAME[] = "validate.host.name";
const char PARAM_CLIENT_STATS_INTERVAL[] = "stats.interval";

struct modConfData_s
{
	rsconf_t * pConf; /* our overall config object */

	char * endpoint_;
	pulsar_client_t * client_;
	pulsar_client_configuration_t * client_config_;

	pthread_mutex_t prod_mutex_;
	struct hashmap * producers_;
};
static modConfData_t * loadModConf = NULL; /* modConf ptr to use for the current load process */
static modConfData_t * runModConf = NULL; /* modConf ptr to use for the current load process */

/* module-global parameters */
static struct cnfparamdescr modpdescr[] = {
	{PARAM_ENDPOINT, eCmdHdlrString, CNFPARAM_REQUIRED},
	{PARAM_CLIENT_MEMORY_LIMIT, eCmdHdlrPositiveInt, 0},
	// {PARAM_CLIENT_AUTH, eCmdHdlrNonNegInt, 0},  // enum ?
	// {PARAM_CLIENT_AUTH_PARAM, eCmdHdlrString, 0},
	{PARAM_CLIENT_OPERATION_TIMEOUT, eCmdHdlrPositiveInt, 0},
	{PARAM_CLIENT_IO_THREADS, eCmdHdlrPositiveInt, 0},
	{PARAM_CLIENT_LISTENER_THREADS, eCmdHdlrPositiveInt, 0},
	{PARAM_CLIENT_LOOKUP_REQUESTS, eCmdHdlrPositiveInt, 0},
	{PARAM_CLIENT_USE_TLS, eCmdHdlrNonNegInt, 0},  // bool
	{PARAM_CLIENT_TLS_TRUST_CERT, eCmdHdlrGetWord, 0},  // string?
	{PARAM_CLIENT_TLS_ALLOW_INSECURE_CONN, eCmdHdlrNonNegInt, 0},  // bool
	{PARAM_CLIENT_VALIDATE_HOST_NAME, eCmdHdlrNonNegInt, 0},  // bool
	{PARAM_CLIENT_STATS_INTERVAL, eCmdHdlrPositiveInt, 0},
};
static struct cnfparamblk modpblk = {CNFPARAMBLK_VERSION, sizeof(modpdescr) / sizeof(struct cnfparamdescr), modpdescr};

// action settings
const char PARAM_PRODUCER_TOPIC[] = "topic";
const char PARAM_PRODUCER_SEND_TIMEOUT[] = "send.timeout";
const char PARAM_PRODUCER_INITIAL_SEQUENCE[] = "initial.sequence.id";
const char PARAM_PRODUCER_COMPRESSION[] = "compression.type";
const char PARAM_PRODUCER_MAX_PENDING_MESSAGES[] = "max.pending.messages";
const char PARAM_PRODUCER_MAX_PENDING_MESSAGES_ACROSS_PARTITIONS[] = "max.pending.messages.across.partitions";
const char PARAM_PRODUCER_PARTITIONS_ROUTING_MODE[] = "partitions.routing.mode";
const char PARAM_PRODUCER_HASHING_SCHEME[] = "hashing.scheme";
const char PARAM_PRODUCER_LAZY_START_PART_PRODUCERS[] = "lazy.start.partitioned.producers";
const char PARAM_PRODUCER_BLOCK_FULL_QUEUE[] = "block.full.queue";
const char PARAM_PRODUCER_BATCHING[] = "batching";
const char PARAM_PRODUCER_BATCHING_MAX_MESSAGES[] = "batching.max.messages";
const char PARAM_PRODUCER_BATCHING_MAX_SIZE[] = "batching.max.allowed.size";
const char PARAM_PRODUCER_BATCHING_MAX_DELAY[] = "batching.max.publish.delay";
const char PARAM_PRODUCER_CHUNKING[] = "chunking";
const char PARAM_TEMPLATE[] = "template";

/* tables for interfacing with the v6 config system */
/* action (instance) parameters */
static struct cnfparamdescr actpdescr[] = {
	{PARAM_PRODUCER_TOPIC, eCmdHdlrString, CNFPARAM_REQUIRED},
	{PARAM_PRODUCER_SEND_TIMEOUT, eCmdHdlrPositiveInt, 0},
	{PARAM_PRODUCER_INITIAL_SEQUENCE, eCmdHdlrInt, 0},
	{PARAM_PRODUCER_COMPRESSION, eCmdHdlrNonNegInt, 0},  // enum / eCmdHdlrGetWord?
	{PARAM_PRODUCER_MAX_PENDING_MESSAGES, eCmdHdlrPositiveInt, 0},
	{PARAM_PRODUCER_MAX_PENDING_MESSAGES_ACROSS_PARTITIONS, eCmdHdlrPositiveInt, 0},
	{PARAM_PRODUCER_PARTITIONS_ROUTING_MODE, eCmdHdlrNonNegInt, 0},  // enum / eCmdHdlrGetWord?
	{PARAM_PRODUCER_HASHING_SCHEME, eCmdHdlrNonNegInt, 0},  // enum / eCmdHdlrGetWord?
	{PARAM_PRODUCER_LAZY_START_PART_PRODUCERS, eCmdHdlrNonNegInt, 0},  // bool
	{PARAM_PRODUCER_BLOCK_FULL_QUEUE, eCmdHdlrNonNegInt, 0},  // bool
	{PARAM_PRODUCER_BATCHING, eCmdHdlrNonNegInt, 0},  // bool
	{PARAM_PRODUCER_BATCHING_MAX_MESSAGES, eCmdHdlrPositiveInt, 0},
	{PARAM_PRODUCER_BATCHING_MAX_SIZE, eCmdHdlrPositiveInt, 0},
	{PARAM_PRODUCER_BATCHING_MAX_DELAY, eCmdHdlrPositiveInt, 0},
	{PARAM_PRODUCER_CHUNKING, eCmdHdlrNonNegInt, 0},  // bool

	{PARAM_TEMPLATE, eCmdHdlrString, 0},  // string
};
static struct cnfparamblk actpblk = {CNFPARAMBLK_VERSION, sizeof(actpdescr) / sizeof(struct cnfparamdescr), actpdescr};

rsRetVal createInstance(instanceData ** data)
{
	instanceData * instance_data = calloc(1, sizeof(instanceData)); /* use this to point to data elements */
	if (instance_data == NULL) {
		*data = NULL;
		return RS_RET_OUT_OF_MEMORY;
	}

	instance_data->template_ = NULL;
	instance_data->producer_ = NULL;

	*data = instance_data;
	return RS_RET_OK;
}

// finishing plugin
EXPORT_FUNC rsRetVal doHUP(instanceData *)
{
	return RS_RET_OK;
}

EXPORT_FUNC rsRetVal createWrkrInstance(wrkrInstanceData_t ** worker_data, instanceData * data)
{
	wrkrInstanceData_t * const worker = calloc(1, sizeof(wrkrInstanceData_t));
	if (worker == NULL) {
		*worker_data = NULL;
		return RS_RET_OUT_OF_MEMORY;
	}

	worker->data = data;
	*worker_data = worker;
	return RS_RET_OK;
}

EXPORT_FUNC rsRetVal isCompatibleWithFeature(syslogFeature efeat)
{
	return RS_RET_INCOMPATIBLE;
}

EXPORT_FUNC rsRetVal freeInstance(void * module_data)
{
	instanceData * data = (instanceData *)module_data;
	free(data->template_);
	free(data);
	return RS_RET_OK;
}

EXPORT_FUNC rsRetVal freeWrkrInstance(void * pd)
{
	wrkrInstanceData_t * worker_data = (wrkrInstanceData_t *)pd;
	free(worker_data);
	return RS_RET_OK;
}

EXPORT_FUNC rsRetVal tryResume(wrkrInstanceData_t ATTR_UNUSED * worker_data)
{
	return RS_RET_OK;
}

EXPORT_FUNC rsRetVal doAction(void * msg_data, wrkrInstanceData_t * worker_data)
{
	uchar ** strings = (uchar **)msg_data;

	pulsar_message_t * const pulsar_message = pulsar_message_create();
	pulsar_message_set_content(pulsar_message, strings[0], strlen(strings[0]));

	pulsar_producer_send(worker_data->data->producer_, pulsar_message);

	pulsar_message_free(pulsar_message);

	return RS_RET_OK;
}

EXPORT_FUNC rsRetVal newActInst(uchar ATTR_UNUSED * modName,
								struct nvlst * lst,
								void ** ppModData,
								omodStringRequest_t ** ppOMSR)
{
	DEFiRet;

	*ppOMSR = NULL;

	struct cnfparamvals * pvals = nvlstGetParams(lst, &actpblk, NULL);
	if (pvals == NULL) {
		LogError(0, RS_RET_MISSING_CNFPARAMS, "ompulsar: error reading config parameters");
		ABORT_FINALIZE(RS_RET_MISSING_CNFPARAMS);
	}

	if (Debug) {
		dbgprintf("action param blk in ompulsar:\n");
		cnfparamsPrint(&actpblk, pvals);
	}

	instanceData * instance_data;
	CHKiRet(createInstance(&instance_data));

	char * topic;
	pulsar_producer_configuration_t * conf = pulsar_producer_configuration_create();

	struct producer_tuple * p = NULL;
	for (int i = 0; i < actpblk.nParams && p == NULL; ++i) {
		if (!pvals[i].bUsed) {
			continue;
		}

		if (strcmp(actpblk.descr[i].name, PARAM_PRODUCER_TOPIC) == 0) {
			topic = es_str2cstr(pvals[i].val.d.estr, NULL);
			pthread_mutex_lock(&loadModConf->prod_mutex_);
			p = hashmap_get(loadModConf->producers_, &(struct producer_tuple){.topic_ = topic});
			pthread_mutex_unlock(&loadModConf->prod_mutex_);
		} else if (strcmp(actpblk.descr[i].name, PARAM_TEMPLATE) == 0) {
			instance_data->template_ = es_str2cstr(pvals[i].val.d.estr, NULL);
		} else if (strcmp(actpblk.descr[i].name, PARAM_PRODUCER_SEND_TIMEOUT) == 0) {
			pulsar_producer_configuration_set_send_timeout(conf, (int)pvals[i].val.d.n);
		} else if (strcmp(actpblk.descr[i].name, PARAM_PRODUCER_INITIAL_SEQUENCE) == 0) {
			pulsar_producer_configuration_set_initial_sequence_id(conf, (int64_t)pvals[i].val.d.n);
		} else if (strcmp(actpblk.descr[i].name, PARAM_PRODUCER_COMPRESSION) == 0) {
			pulsar_producer_configuration_set_initial_sequence_id(conf, (pulsar_compression_type)pvals[i].val.d.n);
		} else if (strcmp(actpblk.descr[i].name, PARAM_PRODUCER_MAX_PENDING_MESSAGES) == 0) {
			pulsar_producer_configuration_set_max_pending_messages(conf, (int)pvals[i].val.d.n);
		} else if (strcmp(actpblk.descr[i].name, PARAM_PRODUCER_MAX_PENDING_MESSAGES_ACROSS_PARTITIONS) == 0) {
			pulsar_producer_configuration_set_max_pending_messages_across_partitions(conf, (int)pvals[i].val.d.n);
		} else if (strcmp(actpblk.descr[i].name, PARAM_PRODUCER_PARTITIONS_ROUTING_MODE) == 0) {
			pulsar_producer_configuration_set_partitions_routing_mode(conf,
																	  (pulsar_partitions_routing_mode)pvals[i].val.d.n);
		} else if (strcmp(actpblk.descr[i].name, PARAM_PRODUCER_HASHING_SCHEME) == 0) {
			pulsar_producer_configuration_set_hashing_scheme(conf, (pulsar_hashing_scheme)pvals[i].val.d.n);
		} else if (strcmp(actpblk.descr[i].name, PARAM_PRODUCER_LAZY_START_PART_PRODUCERS) == 0) {
			pulsar_producer_configuration_set_lazy_start_partitioned_producers(conf, (int)pvals[i].val.d.n);
		} else if (strcmp(actpblk.descr[i].name, PARAM_PRODUCER_BLOCK_FULL_QUEUE) == 0) {
			pulsar_producer_configuration_set_block_if_queue_full(conf, (int)pvals[i].val.d.n);
		} else if (strcmp(actpblk.descr[i].name, PARAM_PRODUCER_BATCHING) == 0) {
			pulsar_producer_configuration_set_batching_enabled(conf, (int)pvals[i].val.d.n);
		} else if (strcmp(actpblk.descr[i].name, PARAM_PRODUCER_BATCHING_MAX_MESSAGES) == 0) {
			pulsar_producer_configuration_set_batching_max_messages(conf, (unsigned int)pvals[i].val.d.n);
		} else if (strcmp(actpblk.descr[i].name, PARAM_PRODUCER_BATCHING_MAX_SIZE) == 0) {
			pulsar_producer_configuration_set_batching_max_allowed_size_in_bytes(conf, (unsigned long)pvals[i].val.d.n);
		} else if (strcmp(actpblk.descr[i].name, PARAM_PRODUCER_BATCHING_MAX_DELAY) == 0) {
			pulsar_producer_configuration_set_batching_max_publish_delay_ms(conf, (unsigned long)pvals[i].val.d.n);
		} else if (strcmp(actpblk.descr[i].name, PARAM_PRODUCER_CHUNKING) == 0) {
			pulsar_producer_configuration_set_chunking_enabled(conf, (int)pvals[i].val.d.n);
		} else {
			DBGPRINTF(
				"ompulsar: program warning, non-handled "
				"param '%s'\n",
				actpblk.descr[i].name);
		}
	}

	if (p == NULL) {
		pulsar_producer_t * producer;
		pulsar_client_create_producer(loadModConf->client_, topic, conf, &producer);

		pthread_mutex_lock(&loadModConf->prod_mutex_);
		hashmap_set(loadModConf->producers_,
					&(struct producer_tuple){.topic_ = topic, .producer_ = producer, .producer_config_ = conf});
		pthread_mutex_unlock(&loadModConf->prod_mutex_);

		instance_data->producer_ = producer;
	} else {
		pulsar_producer_configuration_free(conf);
		conf = NULL;

		free(topic);
		topic = NULL;

		instance_data->producer_ = p->producer_;
	}

	// TODO: validate required parameters and values

	CHKiRet(OMSRconstruct(ppOMSR, 1));
	CHKiRet(OMSRsetEntry(
		*ppOMSR,
		0,
		(uchar *)strdup(instance_data->template_ == NULL ? "RSYSLOG_StdJSONFmt" : instance_data->template_),
		OMSR_NO_RQD_TPL_OPTS));

finalize_it:
	if (iRet == RS_RET_OK || iRet == RS_RET_SUSPENDED) {
		*ppModData = instance_data;
	} else {
		/* cleanup, we failed */
		if (*ppOMSR != NULL) {
			OMSRdestruct(*ppOMSR);
			*ppOMSR = NULL;
		}
		if (instance_data != NULL) {
			freeInstance(instance_data);
		}
	}

	cnfparamvalsDestruct(pvals, &actpblk);
	RETiRet;
}

EXPORT_FUNC rsRetVal dbgPrintInstInfo(void ATTR_UNUSED * pModData)
{
	DEFiRet;
	RETiRet;
}

EXPORT_FUNC rsRetVal modExit()
{
	DEFiRet;
	RETiRet;
}

EXPORT_FUNC rsRetVal beginCnfLoad(modConfData_t ** ptr, rsconf_t * rs_config)
{
	DEFiRet;

	modConfData_t * module_config = calloc(1, sizeof(modConfData_t));
	if (module_config == NULL) {
		*ptr = NULL;
		return RS_RET_OUT_OF_MEMORY;
	}

	module_config->pConf = rs_config;
	pthread_mutex_init(&module_config->prod_mutex_, NULL);
	module_config->producers_ = hashmap_new(
		sizeof(struct producer_tuple), 0, 0, 0, producer_tuple_hash, producer_tuple_compare, producer_tuple_free, NULL);

	loadModConf = module_config;

	*ptr = module_config;

	RETiRet;
}

EXPORT_FUNC rsRetVal setModCnf(struct nvlst * lst)
{
	DEFiRet;

	struct cnfparamvals * pvals = nvlstGetParams(lst, &modpblk, NULL);
	if (pvals == NULL) {
		LogError(0,
				 RS_RET_MISSING_CNFPARAMS,
				 "error processing module "
				 "config parameters [module(...)]");
		ABORT_FINALIZE(RS_RET_MISSING_CNFPARAMS);
	}

	if (Debug) {
		dbgprintf(">>> module (global) param blk for ompulsar:\n");
		cnfparamsPrint(&modpblk, pvals);
	}

	if (loadModConf->client_config_ != NULL) {
		LogError(0, RS_RET_LOAD_ERROR, "error processing module. already initialized");
		ABORT_FINALIZE(RS_RET_LOAD_ERROR);
	}

	loadModConf->client_config_ = pulsar_client_configuration_create();

	for (int i = 0; i < modpblk.nParams; ++i) {
		if (!pvals[i].bUsed)
			continue;

		if (strcmp(modpblk.descr[i].name, PARAM_ENDPOINT) == 0) {
			loadModConf->endpoint_ = es_str2cstr(pvals[i].val.d.estr, NULL);
		} else if (strcmp(modpblk.descr[i].name, PARAM_CLIENT_MEMORY_LIMIT) == 0) {
			pulsar_client_configuration_set_memory_limit(loadModConf->client_config_, (uint64_t)pvals[i].val.d.n);
		} else if (strcmp(modpblk.descr[i].name, PARAM_CLIENT_OPERATION_TIMEOUT) == 0) {
			pulsar_client_configuration_set_operation_timeout_seconds(loadModConf->client_config_,
																	  (int)pvals[i].val.d.n);
		} else if (strcmp(modpblk.descr[i].name, PARAM_CLIENT_IO_THREADS) == 0) {
			pulsar_client_configuration_set_io_threads(loadModConf->client_config_, (int)pvals[i].val.d.n);
		} else if (strcmp(modpblk.descr[i].name, PARAM_CLIENT_LISTENER_THREADS) == 0) {
			pulsar_client_configuration_set_message_listener_threads(loadModConf->client_config_,
																	 (int)pvals[i].val.d.n);
		} else if (strcmp(modpblk.descr[i].name, PARAM_CLIENT_LOOKUP_REQUESTS) == 0) {
			pulsar_client_configuration_set_concurrent_lookup_request(loadModConf->client_config_,
																	  (int)pvals[i].val.d.n);
		} else if (strcmp(modpblk.descr[i].name, PARAM_CLIENT_USE_TLS) == 0) {
			pulsar_client_configuration_set_use_tls(loadModConf->client_config_, (int)pvals[i].val.d.n);
		} else if (strcmp(modpblk.descr[i].name, PARAM_CLIENT_TLS_TRUST_CERT) == 0) {
			char * cert_file = es_str2cstr(pvals[i].val.d.estr, NULL);  // todo pass pointer to the function below
			pulsar_client_configuration_set_tls_trust_certs_file_path(loadModConf->client_config_, cert_file);
			free(cert_file);
		} else if (strcmp(modpblk.descr[i].name, PARAM_CLIENT_TLS_ALLOW_INSECURE_CONN) == 0) {
			pulsar_client_configuration_set_tls_allow_insecure_connection(loadModConf->client_config_,
																		  (int)pvals[i].val.d.n);
		} else if (strcmp(modpblk.descr[i].name, PARAM_CLIENT_VALIDATE_HOST_NAME) == 0) {
			pulsar_client_configuration_set_validate_hostname(loadModConf->client_config_, (int)pvals[i].val.d.n);
		} else if (strcmp(modpblk.descr[i].name, PARAM_CLIENT_STATS_INTERVAL) == 0) {
			pulsar_client_configuration_set_stats_interval_in_seconds(loadModConf->client_config_,
																	  (unsigned int)pvals[i].val.d.n);
		} else {
			dbgprintf(
				"ompulsar: program error, non-handled "
				"param '%s' in setModCnf\n",
				modpblk.descr[i].name);
		}
	}

	loadModConf->client_ = pulsar_client_create(loadModConf->endpoint_, loadModConf->client_config_);

finalize_it:
	cnfparamvalsDestruct(pvals, &modpblk);
	RETiRet;
}

EXPORT_FUNC rsRetVal endCnfLoad(modConfData_t ATTR_UNUSED * ptr)
{
	DEFiRet;
	RETiRet;
}

EXPORT_FUNC rsRetVal checkCnf(modConfData_t ATTR_UNUSED * ptr)
{
	DEFiRet;
	RETiRet;
}

EXPORT_FUNC rsRetVal activateCnf(modConfData_t * ptr)
{
	DEFiRet;
	runModConf = ptr;
	RETiRet;
}

EXPORT_FUNC rsRetVal freeCnf(void * ptr)
{
	modConfData_t * mod = (modConfData_t *)ptr;
	DEFiRet;

	free(mod->endpoint_);
	pulsar_client_free(mod->client_);
	pulsar_client_configuration_free(mod->client_config_);

	pthread_mutex_lock(&mod->prod_mutex_);
	hashmap_free(mod->producers_);
	pthread_mutex_unlock(&mod->prod_mutex_);
	pthread_mutex_destroy(&mod->prod_mutex_);

	free(mod);

	RETiRet;
}

EXPORT_FUNC rsRetVal parseSelectorAct(uchar ** pp ATTR_UNUSED,
									  void ** ppModData ATTR_UNUSED,
									  omodStringRequest_t ** ppOMSR ATTR_UNUSED)
{
	return RS_RET_LEGA_ACT_NOT_SUPPORTED;
}

EXPORT_FUNC rsRetVal modGetID(void ** pID)
{
	*pID = STD_LOADABLE_MODULE_ID;
	return RS_RET_OK;
}

EXPORT_FUNC rsRetVal queryEtryPt(uchar * name, rsRetVal (**pEtryPoint)())
{
	DEFiRet;

	if ((name == NULL) || (pEtryPoint == NULL)) {
		return RS_RET_PARAM_ERROR;
	}
	*pEtryPoint = NULL;

	if (strcmp((char *)name, "modExit") == 0) {
		*pEtryPoint = modExit;
	} else if (strcmp((char *)name, "modGetID") == 0) {
		*pEtryPoint = modGetID;
	} else if (strcmp((char *)name, "getType") == 0) {
		*pEtryPoint = modGetType;
	} else if (strcmp((char *)name, "getKeepType") == 0) {
		*pEtryPoint = modGetKeepType;
	} else if (strcmp((char *)name, "doAction") == 0) {
		*pEtryPoint = doAction;
	} else if (strcmp((char *)name, "dbgPrintInstInfo") == 0) {
		*pEtryPoint = dbgPrintInstInfo;
	} else if (strcmp((char *)name, "freeInstance") == 0) {
		*pEtryPoint = freeInstance;
	} else if (strcmp((char *)name, "parseSelectorAct") == 0) {
		*pEtryPoint = parseSelectorAct;
	} else if (strcmp((char *)name, "isCompatibleWithFeature") == 0) {
		*pEtryPoint = isCompatibleWithFeature;
	} else if (strcmp((char *)name, "tryResume") == 0) {
		*pEtryPoint = tryResume;
	} else if (strcmp((char *)name, "createWrkrInstance") == 0) {
		*pEtryPoint = createWrkrInstance;
	} else if (strcmp((char *)name, "freeWrkrInstance") == 0) {
		*pEtryPoint = freeWrkrInstance;
	} else if (strcmp((char *)name, "newActInst") == 0) {
		*pEtryPoint = newActInst;
	} else if (strcmp((char *)name, "getModCnfName") == 0) {
		*pEtryPoint = modGetCnfName;
	} else if (strcmp((char *)name, "setModCnf") == 0) {
		*pEtryPoint = setModCnf;
	} else if (strcmp((char *)name, "beginCnfLoad") == 0) {
		*pEtryPoint = beginCnfLoad;
	} else if (strcmp((char *)name, "endCnfLoad") == 0) {
		*pEtryPoint = endCnfLoad;
	} else if (strcmp((char *)name, "checkCnf") == 0) {
		*pEtryPoint = checkCnf;
	} else if (strcmp((char *)name, "activateCnf") == 0) {
		*pEtryPoint = activateCnf;
	} else if (strcmp((char *)name, "freeCnf") == 0) {
		*pEtryPoint = freeCnf;
	} else if (strcmp((char *)name, "doHUP") == 0) {
		*pEtryPoint = doHUP;
	}

	if (iRet == RS_RET_OK && *pEtryPoint == NULL) {
		dbgprintf("entry point '%s' not present in module\n", name);
		iRet = RS_RET_MODULE_ENTRY_POINT_NOT_FOUND;
	}

	RETiRet;
}

EXPORT_FUNC rsRetVal modInit(ATTR_UNUSED int iIFVersRequested,
							 int * ipIFVersProvided,
							 rsRetVal (**pQueryEtryPt)(),
							 rsRetVal (*pHostQueryEtryPt)(uchar *, rsRetVal (**)()),
							 ATTR_UNUSED modInfo_t * pModInfo)
{
	DEFiRet;

	assert(pHostQueryEtryPt != NULL);

	rsRetVal (*pObjGetObjInterface)(obj_if_t * pIf);
	iRet = pHostQueryEtryPt((uchar *)"objGetObjInterface", &pObjGetObjInterface);
	if ((iRet != RS_RET_OK) || (pQueryEtryPt == NULL) || (ipIFVersProvided == NULL) || (pObjGetObjInterface == NULL)) {
		return (iRet == RS_RET_OK) ? RS_RET_PARAM_ERROR : iRet;
	}

	*pQueryEtryPt = queryEtryPt;

	/* now get the obj interface so that we can access other objects */
	iRet = pObjGetObjInterface(&obj);
	if (iRet != RS_RET_OK)
		return iRet;

	*ipIFVersProvided = CURR_MOD_IF_VERSION; /* we only support the current interface specification */

	iRet = pHostQueryEtryPt((uchar *)"regCfSysLineHdlr", &omsdRegCFSLineHdlr);

	RETiRet;
}
