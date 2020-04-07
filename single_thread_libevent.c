#include <aerospike/aerospike.h>
#include <aerospike/aerospike_batch.h>
#include <aerospike/aerospike_key.h>
#include <aerospike/as_event.h>
#include <aerospike/as_monitor.h>
#include <unistd.h>
#include <event.h>

/******************************************************************************
 *	Types
 *****************************************************************************/

// External loop definition
typedef struct {
	pthread_t thread;
	struct event_base* event_loop;
	as_event_loop* as_loop;
} loop;

typedef struct {
	uint32_t next_id;     // Key of next record to write.
	uint32_t max;         // Number of records to write.
	uint32_t count;       // Records written.
	uint32_t queue_size;  // Maximum records allowed inflight (in async queue).
	uint32_t pipe_count;  // Records in pipeline. Pipeline mode only.
	as_pipe_listener pipe_listener;  // Pipeline listener callback. Pipeline mode only.
} counter;

/******************************************************************************
 *	Globals
 *****************************************************************************/

static const char* g_host = "127.0.0.1";
static int g_port = 3000;
static const char* g_namespace = "test";
static const char* g_set = "test";

static aerospike as;

/******************************************************************************
 *	Forward Declarations
 *****************************************************************************/

static void write_records_async(counter* counter);
static bool write_record(as_event_loop* event_loop, counter* counter);
static void write_listener(as_error* err, void* udata, as_event_loop* event_loop);
static void batch_read(as_event_loop* event_loop, uint32_t max_records);
static void batch_listener(as_error* err, as_batch_read_records* records, void* udata, as_event_loop* event_loop);
static void close_aerospike(as_event_loop* event_loop);

/******************************************************************************
 *	Functions
 *****************************************************************************/

int
main(int argc, char* argv[])
{
	loop shared_loop;
	uint32_t max_records = 5000;
	int c;
	
	while ((c = getopt(argc, argv, "h:p:n:s:")) != -1) {
		switch (c) {
			case 'h':
				g_host = optarg;
				break;
			case 'p':
				g_port = atoi(optarg);
				break;
			case 'n':
				g_namespace = optarg;
				break;
			case 's':
				g_set = optarg;
				break;
			default:
				printf("Usage: %s [-h <host>] [-p <port>] [-n <namespace>] [-s <set>]\n", argv[0]);
				return 0;
		}
	}
	
	printf("Host=%s:%d\n", g_host, g_port);
	printf("Namespace=%s\n", g_namespace);
	printf("Set=%s\n", g_set);
	
	// Tell client to not call evthread_use_pthreads().
	as_event_set_single_thread(true);

	// Tell C client the maximum number of event loops that will be shared.
	if (! as_event_set_external_loop_capacity(1)) {
		printf("Failed to initialize event loop capacity\n");
		return -1;
	}

	shared_loop.event_loop = event_base_new();
	shared_loop.as_loop = as_event_set_external_loop(shared_loop.event_loop);

	as_config cfg;
	as_config_init(&cfg);
	as_config_add_host(&cfg, g_host, g_port);
	cfg.async_max_conns_per_node = 100; // Limit number of connections to each node.
	cfg.thread_pool_size = 0;  // Disable sync command thread pool.
	cfg.tend_thread_cpu = 0;  // Assign tend thread to cpu core 0.
	aerospike_init(&as, &cfg);
	
	// Connect to cluster.
	as_error err;
	if (aerospike_connect(&as, &err) != AEROSPIKE_OK) {
		printf("Failed to connect to cluster\n");
		aerospike_destroy(&as);
		as_event_close_loops();
		return -1;
	}

	as_event_loop_register_aerospike(shared_loop.as_loop, &as);
	
	// Demonstrate async non-pipelined writes.
	// Async queue size (100) is less because there is one socket per concurrent command.
	counter counter = {
		.next_id = 0,
		.max = max_records,
		.count = 0,
		.queue_size = 100,
		.pipe_count = 0,
		.pipe_listener = NULL
	};
	write_records_async(&counter);
	
	event_base_dispatch(shared_loop.event_loop);
	event_base_free(shared_loop.event_loop);
	as_event_destroy_loops();
}

static void
write_records_async(counter* counter)
{
	// Use same event loop for all records.
	as_event_loop* event_loop = as_event_loop_get();
	
	// Write queue_size commands on the async queue.
	for (uint32_t i = 0; i < counter->queue_size; i++) {
		if (! write_record(event_loop, counter)) {
			break;
		}
	}
}

static bool
write_record(as_event_loop* event_loop, counter* counter)
{
	int64_t id = counter->next_id++;
	
	// No need to destroy a stack as_key object, if we only use as_key_init_int64().
	as_key key;
	as_key_init_int64(&key, g_namespace, g_set, id);
	
	// Create an as_record object with one (integer value) bin. By using
	// as_record_inita(), we won't need to destroy the record if we only set
	// bins using as_record_set_int64().
	as_record rec;
	as_record_inita(&rec, 1);
	
	// In general it's ok to reset a bin value - all as_record_set_... calls
	// destroy any previous value.
	as_record_set_int64(&rec, "test-bin", id);
	
	// Write a record to the database.
	as_error err;
	if (aerospike_key_put_async(&as, &err, NULL, &key, &rec, write_listener, counter, event_loop, counter->pipe_listener) != AEROSPIKE_OK) {
		write_listener(&err, counter, event_loop);
		return false;
	}
	return true;
}

static void
write_listener(as_error* err, void* udata, as_event_loop* event_loop)
{
	counter* counter = udata;
	
	if (err) {
		printf("aerospike_key_put_async() returned %d - %s\n", err->code, err->message);
		close_aerospike(event_loop);
		return;
	}

	// Atomic increment is not necessary since only one event loop is used.
	if (++counter->count == counter->max) {
		// We have written all records.
		printf("Wrote %u records\n", counter->count);
		
		// Records can now be read in a batch.
		batch_read(event_loop, counter->max);
		return;
	}
	
	// Check if we need to write another record.
	if (counter->next_id < counter->max) {
		write_record(event_loop, counter);
	}
	else {
		if (counter->pipe_listener) {
			// There's one fewer command in the pipeline.
			counter->pipe_count--;
		}
	}
}

static void
batch_read(as_event_loop* event_loop, uint32_t max_records)
{
	// Make a batch of all the keys we inserted.
	as_batch_read_records* records = as_batch_read_create(max_records);
	
	for (uint32_t i = 0; i < max_records; i++) {
		as_batch_read_record* record = as_batch_read_reserve(records);
		as_key_init_int64(&record->key, g_namespace, g_set, (int64_t)i);
		record->read_all_bins = true;
	}
	
	// Read these keys.
	as_error err;
	if (aerospike_batch_read_async(&as, &err, NULL, records, batch_listener, NULL, event_loop) != AEROSPIKE_OK) {
		batch_listener(&err, records, NULL, event_loop);
	}
}

static void
batch_listener(as_error* err, as_batch_read_records* records, void* udata, as_event_loop* event_loop)
{
	if (err) {
		printf("aerospike_batch_read_async() returned %d - %s\n", err->code, err->message);
		as_batch_read_destroy(records);
		close_aerospike(event_loop);
		return;
	}

	as_vector* list = &records->list;

	uint32_t n_found = 0;

	for (uint32_t i = 0; i < list->size; i++) {
		as_batch_read_record* record = as_vector_get(list, i);

		if (record->result == AEROSPIKE_OK) {
			n_found++;
		}
		else if (record->result == AEROSPIKE_ERR_RECORD_NOT_FOUND) {
			// The transaction succeeded but the record doesn't exist.
			printf("AEROSPIKE_ERR_RECORD_NOT_FOUND\n");
		}
		else {
			// The transaction failed.
			printf("Error %d\n", record->result);
		}
	}

	printf("Found %u/%u records\n", n_found, list->size);
	as_batch_read_destroy(records);
	close_aerospike(event_loop);
}

static void
destroy_aerospike(void* udata)
{
	as_event_loop* event_loop = udata;

	as_error err;
	aerospike_close(&as, &err);
	aerospike_destroy(&as);
	as_event_close_loop(event_loop);
}

static void
close_aerospike(as_event_loop* event_loop)
{
	as_event_loop_close_aerospike(event_loop, &as, destroy_aerospike, event_loop);
}
