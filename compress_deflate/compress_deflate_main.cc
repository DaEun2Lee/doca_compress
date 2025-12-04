/*
 * Copyright (c) 2022-2023 NVIDIA CORPORATION & AFFILIATES, ALL RIGHTS RESERVED.
 *
 * This software product is a proprietary product of NVIDIA CORPORATION &
 * AFFILIATES (the "Company") and all right, title, and interest in and to the
 * software product, including all associated intellectual property rights, are
 * and shall remain exclusively with the Company.
 *
 * This software product is governed by the End User License Agreement
 * provided with the software product.
 *
 */

#include <stdlib.h>
#include <string.h>

#include <doca_argp.h>
#include <doca_compress.h>
#include <doca_dev.h>
#include <doca_error.h>
#include <doca_log.h>

#include <utils.h>

#include "compress_common.h"

DOCA_LOG_REGISTER(COMPRESS_DEFLATE::MAIN);

/* Sample's Logic */
doca_error_t compress_deflate(struct compress_cfg *cfg, char *file_data, size_t file_size);

/*
 * Sample main function
 *
 * @argc [in]: command line arguments size
 * @argv [in]: array of command line arguments
 * @return: EXIT_SUCCESS on success and EXIT_FAILURE otherwise
 */
int
main(int argc, char **argv)
{
	doca_error_t result;
	struct compress_cfg compress_cfg;
	char *file_data = NULL;
	size_t file_size;
	struct doca_log_backend *sdk_log;
	int exit_status = EXIT_FAILURE;

	strcpy(compress_cfg.pci_address, "03:00.0");
	strcpy(compress_cfg.file_path, "data_to_compress.txt");
	strcpy(compress_cfg.output_path, "out.txt");
	compress_cfg.output_checksum = false;

	/* Register a logger backend */
	result = doca_log_backend_create_standard();
	if (result != DOCA_SUCCESS)
		goto sample_exit;

	/* Register a logger backend for internal SDK errors and warnings */
	result = doca_log_backend_create_with_file_sdk(stderr, &sdk_log);
	if (result != DOCA_SUCCESS)
		goto sample_exit;
	result = doca_log_backend_set_sdk_level(sdk_log, DOCA_LOG_LEVEL_WARNING);
	if (result != DOCA_SUCCESS)
		goto sample_exit;

	DOCA_LOG_INFO("Starting the sample");

	/* The pkg-config (*.pc file) for the Arg Parser library is doca-argp. */
	result = doca_argp_init("doca_compress_deflate", &compress_cfg);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to init ARGP resources: %s", doca_error_get_descr(result));
		goto sample_exit;
	}

	/* This function is defined in compress_common.h */
	/* TODO: parameter 변경에 따른 함수 변경 필요 */
	result = register_compress_params();
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to register ARGP params: %s", doca_error_get_descr(result));
		goto argp_cleanup;
	}

	/*
	*- 인수 구문 분석: 프로그램의 main 함수로 전달된 argc와 argv 배열을 검토하여, doca_argp_init()에서 정의된 옵션(doca_argp_add_argument로 등록된)과 일치하는지 확인합니다.
	*- 값 추출 및 저장: 일치하는 옵션이 발견되면, 해당 옵션의 값(인수)을 추출하여 개발자가 지정한 **대상 변수(Target variable)**에 저장합니다.
	*- 도움말 메시지 처리: 사용자가 표준 도움말 요청 인수(예: -h 또는 --help)를 제공했을 경우, 옵션 목록을 출력하고 프로그램을 종료하는 기능을 수행합니다.
	*-  유효성 검사 및 오류 처리: 정의되지 않은 인수나 필수 인수가 누락된 경우와 같은 오류를 감지하고, 해당 오류 메시지를 출력하며 적절한 에러 코드를 반환합니다.
	*/
	/* TODO: 이 함수는 사용 안함 */
	result = doca_argp_start(argc, argv);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to parse sample input: %s", doca_error_get_descr(result));
		goto argp_cleanup;
	}

	/* TODO: 이 함수는 사용 안함 */
	result = read_file(compress_cfg.file_path, &file_data, &file_size);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to read file: %s", doca_error_get_descr(result));
		goto argp_cleanup;
	}

	/* This function is defined in compress_deflate_sample.cc */
	/* TODO: 함수 변경 필요 */
	result = compress_deflate(&compress_cfg, file_data, file_size);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("compress_deflate() encountered an error: %s", doca_error_get_descr(result));
		goto data_file_cleanup;
	}

	exit_status = EXIT_SUCCESS;

data_file_cleanup:
	if (file_data != NULL)
		free(file_data);
argp_cleanup:
	doca_argp_destroy();
sample_exit:
	if (exit_status == EXIT_SUCCESS)
		DOCA_LOG_INFO("Sample finished successfully");
	else
		DOCA_LOG_INFO("Sample finished with errors");
	return exit_status;
}
