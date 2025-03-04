#include "grep.h"

#include <pthread.h>
#include "job_queue.h"
#include <stdlib.h>
#include <stdio.h>

/* Convoluted structs and functions declared here are local to this file! */
/* Except the implementation of grep_files() for grep.h */

/* Generic thread arguments for functions that use job queue */
typedef struct ThreadArguments {
	JobQueue* job_queue;
	int thread_index;
} ThreadArguments;

/* Task structure for grep_file() job */
typedef struct GrepFileTask {
	char* file_name;
	const GrepOptions* options;
} GrepFileTask;

/* Generic thread function that calls grep_file() */
void* thread_grep_file(void* arguments) {
	ThreadArguments* args = (ThreadArguments*)arguments;

	/* Variables returned from this function must be heap allocated */
	int* match_count = malloc(sizeof(int));

	GrepFileTask* task = NULL;
	while (1) {
		task = (GrepFileTask*)job_queue_pop(args->job_queue);
		if (task == NULL) { break; }

		/* grep_file() logic starts */
		GrepFileResult grep_file_result;
		grep_file_result = grep_file(task->file_name, task->options);
		*match_count += grep_file_result.match_count;

		printf("%s[%d]", ANSI_COLOR_YELLOW, args->thread_index + 1);
		printf("%s ", ANSI_COLOR_RESET);
		printf("%s%s", ANSI_COLOR_MAGENTA, task->file_name);
		printf("%s:%s", ANSI_COLOR_CYAN, ANSI_COLOR_RESET);
		printf(" %zu\n", grep_file_result.match_count);
		/* grep_file() logic ends */

		free(task);
	}

	free(args); // freeing consumed arguments
	return (void*)match_count;
}

GrepFilesResult grep_files(char** file_names, int file_names_length, GrepOptions* options) {
	GrepFilesResult grep_files_result;
	grep_files_result.match_count = 0;
	grep_files_result.exit_code = EXIT_SUCCESS;

	/* Not using multithreaded logic if only 1 thread is available */
	if (options->available_threads == 1) {
		for (int index = 0; index < file_names_length; index++) {
			/* Printing file name before matching lines */
			printf("%s%s", ANSI_COLOR_MAGENTA, file_names[index]);
			printf("%s\n", ANSI_COLOR_RESET);

			GrepFileResult grep_file_result;
			grep_file_result = grep_file(file_names[index], options);
			grep_files_result.match_count += grep_file_result.match_count;
		}
		return grep_files_result;
	}

	/* Enabling hidden internal option to disable line printing */
	options->_quiet = 1;

	/* Creating job queue with tasks for threads to process */
	JobQueue* job_queue = job_queue_new();
	for (int index = 0; index < file_names_length; index++) {
		GrepFileTask* task = malloc(sizeof(GrepFileTask));
		task->file_name = file_names[index];
		task->options = options;
		job_queue_push(job_queue, (void*)task);
	}

	/* Creating and starting threads */
	pthread_t* threads = malloc(options->available_threads * sizeof(pthread_t));
	for (int index = 0; index < options->available_threads; index++) {
		/* Preparing generic thread arguments */
		ThreadArguments* args = malloc(sizeof(ThreadArguments));
		args->job_queue = job_queue;
		args->thread_index = index;

		/* Freeing thread arguments if thread failed to start */
		if (pthread_create(&threads[index], NULL, &thread_grep_file, (void*)args)) {
			free(args);
			continue;
		}
	}

	/* Waiting for threads to finish */
	for (int index = 0; index < options->available_threads; index++) {
		int* match_count;

		/* Ignoring threads that failed to join */
		if (pthread_join(threads[index], (void*)&match_count)) { continue; }

		grep_files_result.match_count += *match_count;
		free(match_count);
	}

	/* Freeing threads and job queue */
	free(threads);
	job_queue_free(job_queue);

	/* Disabling hidden internal option */
	options->_quiet = 0;

	return grep_files_result;
}
