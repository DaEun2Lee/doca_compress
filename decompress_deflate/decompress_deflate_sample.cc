/*
 * Copyright (c) 2023 NVIDIA CORPORATION & AFFILIATES, ALL RIGHTS RESERVED.
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

#include <string.h>
#include <stdlib.h>
#include <time.h>
#include <unistd.h>

#include <doca_buf.h>
#include <doca_buf_inventory.h>
#include <doca_ctx.h>
#include <doca_compress.h>
#include <doca_error.h>
#include <doca_log.h>

#include "common.h"
#include "compress_common.h"

DOCA_LOG_REGISTER(DECOMPRESS_DEFLATE);

/*
 * Run decompress_deflate sample
 *
 * @cfg [in]: Configuration parameters
 * @file_data [in]: file data for the decompress task
 * @file_size [in]: file size
 * @return: DOCA_SUCCESS on success, DOCA_ERROR otherwise.
 */
doca_error_t
decompress_deflate(struct compress_cfg *cfg, char *file_data, size_t file_size)
{
	struct compress_resources resources = {0};
	struct program_core_objects *state;
	struct doca_buf *src_doca_buf;
	struct doca_buf *dst_doca_buf;
	/* The sample will use 2 doca buffers */
	uint32_t max_bufs = 2;
	uint64_t output_checksum;
	char *dst_buffer;
	uint8_t *resp_head;
	size_t data_len;
	FILE *out_file;
	doca_error_t result, tmp_result;
	uint64_t max_buf_size;


	out_file = fopen(cfg->output_path, "wr");
	if (out_file == NULL) {
		DOCA_LOG_ERR("Unable to open output file: %s", cfg->output_path);
		return DOCA_ERROR_NO_MEMORY;
	}

	/* Allocate resources */
	resources.mode = COMPRESS_MODE_DECOMPRESS_DEFLATE;
	/* Allocate DOCA compress resources */
	/* TODO: pcie 변경 필요  */
	result = allocate_compress_resources(cfg->pci_address, max_bufs, &resources);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to allocate compress resources: %s", doca_error_get_descr(result));
		goto close_file;
	}
	state = resources.state;

	/* DEFLATE 압축 방식으로 복원(디컴프레스)할 때 필요한 최대 버퍼 크기를 반환하는 함수 */
	result = doca_compress_cap_task_decompress_deflate_get_max_buf_size(doca_dev_as_devinfo(state->dev), &max_buf_size);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to query decompress max buf size: %s", doca_error_get_descr(result));
		goto destroy_resources;
	}
	/* TODO: file을 분할해서 실행  */
	if (file_size > max_buf_size) {
		DOCA_LOG_ERR("Invalid file size. Should be smaller then %lu", max_buf_size);
		goto destroy_resources;
	}
	/* Start compress context */
	/* doca_ctx 핸들을 입력으로 받아 해당 컨텍스트의 하드웨어 자원을 초기화하고 작업 큐 준비 */
	result = doca_ctx_start(state->ctx);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to start context: %s", doca_error_get_descr(result));
		goto destroy_resources;
	}

	dst_buffer = calloc(1, max_buf_size);
	if (dst_buffer == NULL) {
		result = DOCA_ERROR_NO_MEMORY;
		DOCA_LOG_ERR("Failed to allocate memory: %s", doca_error_get_descr(result));
		goto destroy_resources;
	}

	/*  메모리 맵(mmap) 객체에 특정 메모리 범위(주소와 크기)를 설정하는 함수 */  
	result = doca_mmap_set_memrange(state->dst_mmap, dst_buffer, max_buf_size);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to set mmap memory range: %s", doca_error_get_descr(result));
		goto free_dst_buf;
	}
	/* 생성된 메모리 맵(doca_mmap) 객체를 시작하여 하드웨어 자원(예: 메모리 등록, 디바이스 매핑)을 활성화하는 함수 */
	result = doca_mmap_start(state->dst_mmap);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to start mmap: %s", doca_error_get_descr(result));
		goto free_dst_buf;
	}

	result = doca_mmap_set_memrange(state->src_mmap, file_data, file_size);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to set mmap memory range: %s", doca_error_get_descr(result));
		goto free_dst_buf;
	}

	result = doca_mmap_start(state->src_mmap);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to start mmap: %s", doca_error_get_descr(result));
		goto free_dst_buf;
	}

	/* Construct DOCA buffer for each address range */
	result = doca_buf_inventory_buf_get_by_addr(state->buf_inv, state->src_mmap, file_data, file_size, &src_doca_buf);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Unable to acquire DOCA buffer representing source buffer: %s", doca_error_get_descr(result));
		goto free_dst_buf;
	}

	/* Construct DOCA buffer for each address range */
	result = doca_buf_inventory_buf_get_by_addr(state->buf_inv, state->dst_mmap, dst_buffer, max_buf_size, &dst_doca_buf);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Unable to acquire DOCA buffer representing destination buffer: %s", doca_error_get_descr(result));
		goto destroy_src_buf;
	}

	/* Set data length in doca buffer */
	result = doca_buf_set_data(src_doca_buf, file_data, file_size);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Unable to set DOCA buffer data: %s", doca_error_get_descr(result));
		goto destroy_dst_buf;
	}

	/* Submit decompress task with checksum according to user configuration */
	if (cfg->output_checksum) {
		result = submit_decompress_deflate_task(&resources, src_doca_buf, dst_doca_buf, &output_checksum);
		if (result != DOCA_SUCCESS) {
			DOCA_LOG_ERR("Decompress task failed: %s", doca_error_get_descr(result));
			goto destroy_dst_buf;
		}
	} else {
		result = submit_decompress_deflate_task(&resources, src_doca_buf, dst_doca_buf, NULL);
		if (result != DOCA_SUCCESS) {
			DOCA_LOG_ERR("Decompress task failed: %s", doca_error_get_descr(result));
			goto destroy_dst_buf;
		}
	}

	/* Write the result to output file */
	/* doca_buf 객체의 headroom 영역 시작 주소를 반환하는 함수 */
	doca_buf_get_head(dst_doca_buf, (void **)&resp_head);
	/* doca_buf 객체의 dataroom(실제 데이터 영역)에 저장된 유효 데이터 길이를 반환하는 함수 */
	doca_buf_get_data_len(dst_doca_buf, &data_len);
	fwrite(resp_head, sizeof(uint8_t), data_len, out_file);
	DOCA_LOG_INFO("File was decompressed successfully and saved in: %s", cfg->output_path);
	if (cfg->output_checksum)
		DOCA_LOG_INFO("Checksum is %lu", output_checksum);
destroy_dst_buf:
	/* doca_buf 객체의 참조 카운트(reference count)를 1 감소시키는 함수 */
	tmp_result = doca_buf_dec_refcount(dst_doca_buf, NULL);
	if (tmp_result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to decrease DOCA destination buffer reference count: %s", doca_error_get_descr(tmp_result));
		DOCA_ERROR_PROPAGATE(result, tmp_result);
	}
destroy_src_buf:
	tmp_result = doca_buf_dec_refcount(src_doca_buf, NULL);
	if (tmp_result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to decrease DOCA source buffer reference count: %s", doca_error_get_descr(tmp_result));
		DOCA_ERROR_PROPAGATE(result, tmp_result);
	}
free_dst_buf:
	free(dst_buffer);
destroy_resources:
	/* Destroy DOCA compress resources */
	/* This function is defined in decompress_common.h */
	tmp_result = destroy_compress_resources(&resources);
	if (tmp_result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to destroy compress resources: %s", doca_error_get_descr(tmp_result));
		DOCA_ERROR_PROPAGATE(result, tmp_result);
	}
close_file:
	fclose(out_file);

	return result;
}
