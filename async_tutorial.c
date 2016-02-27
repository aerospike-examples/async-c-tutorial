#include <aerospike/aerospike.h>
#include <aerospike/aerospike_batch.h>
#include <aerospike/aerospike_key.h>
#include <aerospike/as_monitor.h>

/******************************************************************************
 *	Types
 *****************************************************************************/

// External loop definition
typedef struct {
	pthread_t thread;
	struct ev_loop* ev_loop;
	as_event_loop* as_loop;
} loop;

typedef struct {
	uint32_t max;
	uint32_t count;
	uint32_t queue_size;
	uint32_t pipe_count;
	as_pipe_listener pipe_listener;
} counter;

/******************************************************************************
 *	Globals
 *****************************************************************************/

static const char* g_host = "127.0.0.1";
static int g_port = 3000;
static const char* g_namespace = "test";
static const char* g_set = "test";

static aerospike as;
static as_monitor monitor;

/******************************************************************************
 *	Functions
 *****************************************************************************/

static void
print_usage(const char* program)
{
	printf("Usage: %s [-h <host>] [-p <port>] [-n <namespace>] [-s <set>] [-e] [-l]\n", program);
	printf("-e: share event loop\n");
	printf("-l: use pipeline writes\n");
}

static void*
loop_thread(void* udata)
{
	// Create external loop.
	loop* loop = udata;
 	loop->ev_loop = ev_loop_new(EVFLAG_AUTO);

 	// Share event loop with C client.
 	// This must be done in event loop thread.
	loop->as_loop = as_event_set_external_loop(loop->ev_loop);

	// Notify parent thread that external loop has been initialized.
	as_monitor_notify(&monitor);

	ev_loop(loop->ev_loop, 0);
	ev_loop_destroy(loop->ev_loop);
	return NULL;
}

static bool
share_event_loop(loop* loop)
{
	// Tell C client the maximum number of event loops that will be shared.
	if (! as_event_set_external_loop_capacity(1)) {
		return false;
	}

	// Start monitor.
	as_monitor_begin(&monitor);

	// Create event loop thread that will be shared.
	if (pthread_create(&loop->thread, NULL, loop_thread, loop) != 0) {
		return false;
	}

	// Wait till event loop has been initialized.
	as_monitor_wait(&monitor);	
	return true;
}

static void
batch_listener(as_error* err, as_batch_read_records* records, void* udata, as_event_loop* event_loop)
{
	if (err) {
		printf("aerospike_batch_read_async() returned %d - %s\n", err->code, err->message);
		as_batch_read_destroy(records);
		as_monitor_notify(&monitor);
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
	as_monitor_notify(&monitor);	
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

static bool
write_record(as_event_loop* event_loop, counter* counter, uint32_t index);

static void
write_listener(as_error* err, void* udata, as_event_loop* event_loop)
{
	counter* counter = udata;

	if (err) {
		printf("aerospike_key_put_async() returned %d - %s\n", err->code, err->message);
		as_monitor_notify(&monitor);
		return;
	}

	// Atomic increment is not necessary since only one event loop is used.
	if (++counter->count == counter->max) {
		// We have total records.
		printf("Wrote %u records\n", counter->count);

		// Records can now be read in a batch.
		batch_read(event_loop, counter->max);
		return;
	}

	if (counter->pipe_listener) {
		// Check if we need to write more records.
		uint32_t next = counter->count + counter->pipe_count;

		if (next < counter->max) {
			write_record(event_loop, counter, next);
		}
		else {
			// Decrement pipeline queue because a write finished and we did not write a new record.
			counter->pipe_count--;
		}
	}
	else {		
		// Check if we need to write more records.
		uint32_t next = counter->count + counter->queue_size - 1;

		if (next < counter->max) {
			write_record(event_loop, counter, next);
		}
	}
}

static void
pipeline_listener(void* udata, as_event_loop* event_loop)
{
	counter* counter = udata;

	if (counter->pipe_count < counter->queue_size) {
		// Check if we need to write more records.
		uint32_t next = counter->count + counter->pipe_count;

		if (next < counter->max) {
			// Pipeline has more space.  Issue another write.
			counter->pipe_count++;
			write_record(event_loop, counter, next);
		}
	}
}

static bool
write_record(as_event_loop* event_loop, counter* counter, uint32_t index)
{
	// No need to destroy a stack as_key object, if we only use as_key_init_int64().
	as_key key;
	as_key_init_int64(&key, g_namespace, g_set, (int64_t)index);

	// Create an as_record object with one (integer value) bin. By using
	// as_record_inita(), we won't need to destroy the record if we only set
	// bins using as_record_set_int64().
	as_record rec;
	as_record_inita(&rec, 1);

	// In general it's ok to reset a bin value - all as_record_set_... calls
	// destroy any previous value.
	as_record_set_int64(&rec, "test-bin", (int64_t)index);

	// Write a record to the database.
	as_error err;
	if (aerospike_key_put_async(&as, &err, NULL, &key, &rec, write_listener, counter, event_loop, counter->pipe_listener) != AEROSPIKE_OK) {
		write_listener(&err, counter, event_loop);
		return false;
	}
	return true;
}

static void
write_records_pipeline(counter* counter)
{
	// Write a single record to start pipeline.
	// More records will be written in pipeline_listener to fill pipeline queue.
	// A NULL event_loop indicates that an event loop will be chosen round-robin.
	write_record(NULL, counter, 0);
}

static void
write_records_async(counter* counter)
{
	// Use same event loop for all records.
	as_event_loop* event_loop = as_event_loop_get();
		
	// Write queue_size commands on the async queue.
	for (uint32_t i = 0; i < counter->queue_size; i++) {
		if (! write_record(event_loop, counter, i)) {
			break;
		}
	}
}

int
main(int argc, char* argv[])
{
	loop external_loop;
	uint32_t max_records = 5000;
	bool share_loop = false;
	bool pipeline = false;
	int c;

	while ((c = getopt(argc, argv, "h:p:n:s:el")) != -1) {
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
			case 'e':
				share_loop = true;
				break;
			case 'l':
				pipeline = true;
				break;
			default:
				print_usage(argv[0]);
				return 0;
		}
	}

	printf("Host=%s:%d\n", g_host, g_port);
	printf("Namespace=%s\n", g_namespace);
	printf("Set=%s\n", g_set);
	printf("ShareLoop=%s\n", share_loop ? "true" : "false");
	printf("Pipeline=%s\n", pipeline ? "true" : "false");

	// Initialize monitor.
	as_monitor_init(&monitor);

	if (share_loop) {
		// Demonstrate how to share an existing event loop.
		if (! share_event_loop(&external_loop)) {
			printf("Failed to share event loop\n");
			return -1;
		}
	}
	else {
		// Have C client create the event loop.
		if (! as_event_create_loops(1)) {
			printf("Failed to create event loop\n");
			return -1;
		}
	}

	as_config cfg;
	as_config_init(&cfg);
	as_config_add_host(&cfg, g_host, g_port);
	cfg.async_max_conns_per_node = 200;
	cfg.pipe_max_conns_per_node = 32;
	aerospike_init(&as, &cfg);

	// Connect to cluster.
	as_error err;
	if (aerospike_connect(&as, &err) != AEROSPIKE_OK) {
		printf("Failed to connect to cluster\n");
		aerospike_destroy(&as);
		as_event_close_loops();
		return -1;
	}

	// Reset monitor.
	as_monitor_begin(&monitor);

	if (pipeline) {
		// Demonstrate pipelined writes.
		// Pipeline queue size (1000) is greater because sockets are shared. 
		counter counter = {max_records, 0, 1000, 0, pipeline_listener};
		write_records_pipeline(&counter);
	}
	else {
		// Demonstrate async non-pipelined writes.
		// Async queue size (100) is less because there is one socket per concurrent command. 
		counter counter = {max_records, 0, 100, 0, NULL};
		write_records_async(&counter);
	}

	// Wait till all commands have completed before shutting down.
	as_monitor_wait(&monitor);
	aerospike_close(&as, &err);
	aerospike_destroy(&as);
	as_event_close_loops();

	if (share_loop) {
		// Join on external event loop thread.
		pthread_join(external_loop.thread, NULL);
	}
}

