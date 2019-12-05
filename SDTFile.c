#include "SDTFile.h"

// The SPC_data_file_structure.h provided by BH at the time of this writing
// does not compile. We use a fixed copy, generated by the shell script
// fix-spc-data-file-structure-h.sh (which runs in Git Bash).
// In addition, the data structures are fully packed in the files.
#pragma pack(push, 1)
#include "SPC_data_file_structure_fixed.h"
#pragma pack(pop)

#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>


// This is not a general-purpose SDT writer; it only writes the kind of
// histogram data we produce. We write the SDT file as if it was produced
// in FIFO Image mode even though we acquire the photon data in FIFO mode.

// The SDT format is partially documented in the BH TCSPC Handbook and in the
// header file SPC_data_file_structure.h. In practice, the only way to get
// enough information is to inspect actual .sdt files written by BH SPCM.

// This implementation is based on .sdt files written from FIFO Image mode
// measurements in SPCM. The following principles were followed:
// - Avoid storing any incorrect information
// - Store all user-configurable hardware parameters used in FIFO Image mode
// - Avoid storing garbage in fields for parameters not used in FIFO Image mode
//   (as much as possible)
// - Collect all data to be written before writing (no intermixing of queries
//   to hardware or computation of histogram data)


#define NEWLINE "\r\n"


static int WriteSDTIdentification(FILE* fp, const struct SDTFileData *data)
{
	int ret = 0;
	char *buf = NULL;
	size_t bufSize = 0;
	for (;;) {
		bufSize = bufSize ? 2 * bufSize : 512;
		buf = realloc(buf, bufSize);

		// The ID is flanked by EOT (04h) characters in files written by BH
		// SPCM. Not sure why but imitating.
		// Version "3  980 M" is taken from a file written by BH SPCM and
		// analyzed to write this SDT writer code.
		// TODO Test if any of the fields other than ID are required and
		// consider removing.
		snprintf(buf, bufSize,
			"*IDENTIFICAION" NEWLINE
			"  ID        : \x04%s\x04" NEWLINE
			"  Title     : OpenScan FLIM Image" NEWLINE
			"  Version   : 3  980 M" NEWLINE
			"  Revision  : %u bits ADC" NEWLINE
			"  Date      : %s" NEWLINE
			"  Time      : %s" NEWLINE
			"  Author    : Unknown" NEWLINE
			"  Company   : Unknown" NEWLINE
			"  Contents  : FLIM histogram(s) generated by OpenScan" NEWLINE
			"*END" NEWLINE
			NEWLINE,
			fcs_data_identifier, // FIFO Image mode data
			data->histogramBits, data->date, data->time);

		size_t len = strlen(buf);
		if (len >= bufSize - 1) { // May not have fit in buf
			if (bufSize >= 1024 * 1024) {
				ret = 1; // Unterminated string?
				break;
			}
			continue; // Retry with larger buf
		}

		// We do NOT write the null terminator
		size_t written = fwrite(buf, 1, len, fp);
		if (written < len) {
			ret = 1;
		}
		break;
	}
	free(buf);
	return ret;
}


static int WriteSDTEmptySetup(FILE* fp)
{
	const char* setup =
		"*SETUP" NEWLINE
		"*END" NEWLINE
		NEWLINE;
	size_t len = strlen(setup);
	size_t written = fwrite(setup, 1, len, fp);
	if (written < len) {
		return 1; // Write error
	}
	return 0;
}


static int WriteSDTMeasurementDescBlock(FILE* fp,
	const struct SDTFileData *data,
	const struct SDTFileChannelData *channelData,
	const SPCdata *fifoModeParams)
{
	const SPCdata *p = fifoModeParams; // For readability

	MeasureInfo b;
	memset(&b, 0, sizeof(b));

	// Fill in the struct; all known fields are listed in order, even if left
	// zero.

	strncpy(b.time, data->time, sizeof(b.time));
	strncpy(b.date, data->date, sizeof(b.date));
	strncpy(b.mod_ser_no, data->serialNumber, sizeof(b.mod_ser_no));

	// 'meas_mode' is undocumented; SPCM saves 13 in FIFO Image mode
	b.meas_mode = 13;

	b.cfd_ll = p->cfd_limit_low;
	b.cfd_lh = p->cfd_limit_high;
	b.cfd_zc = p->cfd_zc_level;
	b.cfd_hf = p->cfd_holdoff;

	b.syn_zc = p->sync_zc_level;
	b.syn_fd = p->sync_freq_div;
	b.syn_hf = p->sync_holdoff;

	b.tac_r /* s */ = p->tac_range /* ns */ * 1e-9f;
	b.tac_g = p->tac_gain;
	b.tac_of = p->tac_offset;
	b.tac_ll = p->tac_limit_low;
	b.tac_lh = p->tac_limit_high;

	b.adc_re = 1 << data->histogramBits;

	b.eal_de = p->ext_latch_delay;

	// It is not entirely clear what 'ncx' and 'ncy' mean but they appear to be
	// equal to 'img_size_x' and 'img_size_y' when in FIFO Image mode.
	b.ncx = data->numChannels;
	b.ncy = 1;

	// Not applicable to FIFO data
	b.page = 1;

	// We don't use the collection timer or repetition
	b.col_t = 0.0f;
	b.rep_t = 0.0f;
	b.stopt = 0;

	// Not applicable to our FIFO data
	b.overfl = 'N'; // This probably means "do not stop on overflow". TODO Check
	b.use_motor = 0;
	b.steps = 1;
	b.offset = 0.0f; // Memory offset??

	b.dither = p->dither_range;

	// Not applicable to FIFO data
	b.incr = 1;
	b.mem_bank = 0;

	strncpy(b.mod_type, data->modelName, sizeof(b.mod_type));

	b.syn_th = p->sync_threshold;

	// Not applicable to FIFO data
	b.dead_time_comp = 0;

	// Marker polarity does not affect the data, so hard-code to rising-edge.
	// (We support arbitrary marker assignments, so we can't exactly store the
	// hardware settings here.)
	b.polarity_l = data->lineMarkersRecorded ? 1 : 2;
	b.polarity_f = 1;
	b.polarity_p = 1;

	// Not applicable to FIFO data
	b.linediv = 0; // Scan Sync In/Out modes only (see Handbook)
	b.accumulate = 0;
	b.flbck_y = 1;
	b.flbck_x = 1;

	// In theory we could set 'bord_l' to the line delay in pixels, but we
	// we support negative line delays, so just leave it out.
	b.bord_u = 0;
	b.bord_l = 0;

	b.pix_time = (float)(1.0 / data->pixelRateHz);
	b.pix_clk = data->usePixelMarker ? 1 : 0;

	b.trigger = p->trigger;

	// Not applicable to FIFO data (see 'img_*')
	b.scan_x = 0;
	b.scan_y = 0;
	b.scan_rx = 0;
	b.scan_ry = 0;

	// At least some files written by BH SPCM have this set to 0.
	// In any case nobody cares about the FIFO types, most of which are
	// equivalent anyway.
	b.fifo_typ = 0;

	b.epx_div = p->ext_pixclk_div;

	b.mod_type_code = data->modelCode;
	b.mod_fpga_ver = data->fpgaVersion;

	// Not applicable to FIFO data
	b.overflow_corr_factor = 0.0f;
	b.adc_zoom = 0;

	b.cycles = 1;

	// We always stop by command, never by collection timer
	b.StopInfo.status = SPC_CMD_STOP;

	// We always save histograms that consist of (the sum of) whole frames
	// only, so we say that the end of frame was found.
	b.StopInfo.flags =
		((data->pixelMarkersRecorded ? 1 : 0) << 0) | // Pixel clock detected
		((data->lineMarkersRecorded ? 1 : 0) << 1) | // Line clock detected
		((data->frameMarkersRecorded ? 1 : 0) << 2) | // Frame clock detected
		(1 << 7) | // End of frame was found
		(1 << 8) | // First frame and line present
		((data->recordRateCounterRanges ? 1 : 0) << 15);

	// This is meant to be the time when user stopped measurement, so can be
	// longer than the actual end time (see FCSInfo.end_time).
	b.StopInfo.stop_time = data->acquisitionDurationSeconds;

	// Not applicable to our FIFO data
	b.StopInfo.cur_step = 1;
	b.StopInfo.cur_cycle = 1;
	b.StopInfo.cur_page = 1;

	b.StopInfo.min_sync_rate = data->recordRateCounterRanges ? data->minSync : -1.0f;
	b.StopInfo.min_cfd_rate = data->recordRateCounterRanges ? data->minCFD : -1.0f;
	b.StopInfo.min_tac_rate = data->recordRateCounterRanges ? data->minTAC : -1.0f;
	b.StopInfo.min_adc_rate = data->recordRateCounterRanges ? data->minADC : -1.0f;
	b.StopInfo.max_sync_rate = data->recordRateCounterRanges ? data->maxSync : -1.0f;
	b.StopInfo.max_cfd_rate = data->recordRateCounterRanges ? data->maxCFD : -1.0f;
	b.StopInfo.max_tac_rate = data->recordRateCounterRanges ? data->maxTAC : -1.0f;
	b.StopInfo.max_adc_rate = data->recordRateCounterRanges ? data->maxADC : -1.0f;

	b.StopInfo.reserved1;
	b.StopInfo.reserved2;

	b.FCSInfo.chan = channelData->channel;
	b.FCSInfo.fcs_decay_calc = 1 << 5; // 3D image
	b.FCSInfo.mt_resol = data->macroTimeUnitsTenthNs;
	b.FCSInfo.cortime = 1.0; // Not applicable
	b.FCSInfo.calc_photons = channelData->numPhotonsInChannel;
	b.FCSInfo.fcs_points = 0; // Not applicable
	b.FCSInfo.end_time = channelData->timeOfLastPhotonInChannelSeconds;
	b.FCSInfo.overruns = 0; // We fail the acquisition on FIFO overflow
	b.FCSInfo.fcs_type = 0; // Not applicable (I think)
	b.FCSInfo.cross_chan = channelData->channel; // (We're not doing FCCS)
	b.FCSInfo.mod = data->moduleNumber;
	b.FCSInfo.cross_mod = data->moduleNumber;
	b.FCSInfo.cross_mt_resol = data->macroTimeUnitsTenthNs;

	b.image_x = data->width;
	b.image_y = data->height;
	b.image_rx = data->numChannels;
	b.image_ry = 1;

	// Not applicable; SPC-930 only
	b.xy_gain;

	b.dig_flags = (p->master_clock ? 1 : 0) |
		((data->histogramTimeInverted ? 1 : 0) << 2);

	// Not applicable; SPC-930 only
	b.adc_de;
	b.det_type;
	b.x_axis;

	// Not applicable; FIDA, FILDA, and MCS only
	b.HISTInfo;

	b.HISTInfoExt.first_frame_time = data->timeOfFirstFrameMarkerSeconds;
	b.HISTInfoExt.frame_time = data->timeBetweenFrameMarkersSeconds;
	b.HISTInfoExt.line_time = data->timeBetweenLineMarkersSeconds;
	b.HISTInfoExt.pixel_time = data->timeBetweenPixelMarkersSeconds;
	b.HISTInfoExt.scan_type = 0; // Unidirectional
	b.HISTInfoExt.skip_2nd_line_clk = 0; // Do not skip every other edge
	b.HISTInfoExt.right_border = 0; // Skip, for bidirectional scanning only
	b.HISTInfoExt.info; // Reserved

	// Not applicable; USB DELAY-BOX only
	b.sync_delay;
	b.sdel_ser_no;

	// Not applicable; mosaic only
	b.mosaic_ctrl;
	b.mosaic_x = 1;
	b.mosaic_y = 1;
	b.frames_per_el = 1;
	b.chan_per_el = 1;
	b.mosaic_cycles_done;

	// Not applicable; extra devices only
	b.mla_ser_no;
	b.DCC_in_use;
	b.dcc_ser_no;
	b.TiSaLas_status;
	b.TiSaLas_wav;
	b.AOM_status;
	b.AOM_power;
	b.ddg_ser_no;
	b.prior_ser_no;

	// Not applicable; mosaic only
	b.mosaic_x_hi;
	b.mosaic_y_hi;

	b.reserve;

	// (End of the 512-byte MeasureInfo struct)

	size_t written = fwrite(&b, sizeof(b), 1, fp);
	if (written < 1) {
		return 1; // Write error
	}

	return 0;
}


static int WriteSDTHistogramDataBlock(FILE* fp,
	const struct SDTFileData *data,
	const struct SDTFileChannelData *channelData,
	const uint16_t *histogram,
	unsigned *nextBlockOffsetFieldOffset)
{
	size_t numSamples = data->width * data->height * (1 << data->histogramBits);
	long headerOffset = ftell(fp);

	BHFileBlockHeader header;
	memset(&header, 0, sizeof(header));
	
	// block_no or data_offs_ext/next_block_offs_ext is always 0 for us
	header.data_offs = headerOffset + sizeof(header);
	*nextBlockOffsetFieldOffset = headerOffset + offsetof(BHFileBlockHeader, next_block_offs);
	header.block_type = FIFO_DATA | IMG_BLOCK | DATA_USHORT;
	// TODO |= DATA_ZIPPED if we use PKZIP format
	header.meas_desc_block_no = channelData->channel;
	header.lblock_no = (data->moduleNumber << 24) |
		((header.block_type >> 4) & 0xf << 20) | // IMG_BLOCK
		channelData->channel;
	header.block_length = (unsigned long)(numSamples * sizeof(uint16_t));

	size_t written = fwrite(&header, sizeof(header), 1, fp);
	if (written < 1)
		return 1; // Write error

	written = fwrite(histogram, sizeof(uint16_t), numSamples, fp);
	if (written < numSamples)
		return 1; // Write error

	return 0;
}


static unsigned short ModuleTypeToHeaderBits(const char *type)
{
	if (strcmp(type, "SPC-130"    ) == 0) return 0x20;
	if (strcmp(type, "SPC-600"    ) == 0) return 0x21;
	if (strcmp(type, "SPC-630"    ) == 0) return 0x22;
	if (strcmp(type, "SPC-700"    ) == 0) return 0x23;
	if (strcmp(type, "SPC-730"    ) == 0) return 0x24;
	if (strcmp(type, "SPC-830"    ) == 0) return 0x25;
	if (strcmp(type, "SPC-140"    ) == 0) return 0x26;
	if (strcmp(type, "SPC-930"    ) == 0) return 0x27;
	if (strcmp(type, "SPC-150"    ) == 0) return 0x28;
	if (strcmp(type, "DPC-230"    ) == 0) return 0x29;
	if (strcmp(type, "SPC-130EM"  ) == 0) return 0x2a;
	if (strcmp(type, "SPC-160"    ) == 0) return 0x2b;
	if (strcmp(type, "SPC-150N"   ) == 0) return 0x2e;
	if (strcmp(type, "SPC-150NX"  ) == 0) return 0x80;
	if (strcmp(type, "SPC-160X"   ) == 0) return 0x81;
	if (strcmp(type, "SPC-160PCIE") == 0) return 0x82;
	return 0;
}


static unsigned short HeaderChecksum(const bhfile_header *header)
{
	unsigned short *p = (unsigned short *)header;
	unsigned short sum = 0;
	for (int i = 0; i < BH_HDR_LENGTH / 2 - 1; ++i) {
		sum += p[i];
	}
	return -sum + BH_HEADER_CHKSUM;
}


static int WriteSDTFileP(FILE *fp,
	const struct SDTFileData *data,
	const struct SDTFileChannelData *const channelDataArray[],
	const uint16_t *const channelHistograms[],
	const SPCdata *fifoModeParams)
{
	bhfile_header header;
	memset(&header, 0, sizeof(header));

	header.revision = 15 | // Software (file format) revision
		(ModuleTypeToHeaderBits(data->modelName) << 4);
	// TODO Bits 12-15 should be 0x1 for SPC-150NX-12 with 12.5 ns TAC range.
	// How do we determine?

	header.header_valid = BH_HEADER_NOT_VALID;

	// Write the partially filled-in header, marked invalid.
	size_t written = fwrite(&header, sizeof(header), 1, fp);
	if (written < 1) {
		return 1; // Write error
	}
	
	header.info_offs = ftell(fp);
	int err = WriteSDTIdentification(fp, data);
	if (err)
		return err;
	header.info_length = (short)(ftell(fp) - header.info_offs);

	header.setup_offs = ftell(fp);
	err = WriteSDTEmptySetup(fp);
	if (err)
		return err;
	header.setup_length = (short)(ftell(fp) - header.setup_offs);

	header.meas_desc_block_offs = ftell(fp);
	header.no_of_meas_desc_blocks = data->numChannels;
	header.meas_desc_block_length = sizeof(MeasureInfo);
	for (unsigned i = 0; i < data->numChannels; ++i) {
		err = WriteSDTMeasurementDescBlock(fp, data, channelDataArray[i], fifoModeParams);
		if (err)
			return err;
	}

	header.no_of_data_blocks = data->numChannels;
	header.data_block_length = data->width * data->height * (1 << data->histogramBits) * sizeof(uint16_t);
	header.reserved1 = data->numChannels;
	unsigned long nextOffsetPosInPrevBlock = -1;
	for (unsigned i = 0; i < data->numChannels; ++i) {
		long pos = ftell(fp);
		if (i == 0) {
			header.data_block_offs = pos;
		}
		else {
			if (fseek(fp, nextOffsetPosInPrevBlock, SEEK_SET) != 0) {
				return 1; // I/O error
			}
			written = fwrite(&pos, sizeof(unsigned long), 1, fp);
			if (written < 1) {
				return 1; // Write error
			}
			if (fseek(fp, pos, SEEK_SET) != 0) {
				return 1; // I/O error
			}
		}

		err = WriteSDTHistogramDataBlock(fp, data, channelDataArray[i],
			channelHistograms[i], &nextOffsetPosInPrevBlock);
		if (err)
			return err;
	}

	// Rewrite the now-valid header
	header.header_valid = BH_HEADER_VALID;
	header.chksum = HeaderChecksum(&header);

	if (fseek(fp, 0, SEEK_SET) != 0) {
		return 1; // I/O error
	}
	written = fwrite(&header, sizeof(header), 1, fp);
	if (written < 1) {
		return 1; // Write error
	}

	return 0;
}


int WriteSDTFile(const char *filename,
	const struct SDTFileData *data,
	const struct SDTFileChannelData *const channelDataArray[],
	const uint16_t *const channelHistograms[],
	const SPCdata *fifoModeParams)
{
	FILE* fp = fopen(filename, "wb");
	if (!fp) {
		return 1; // Cannot open file
	}

	int err = WriteSDTFileP(fp, data, channelDataArray, channelHistograms, fifoModeParams);
	
	fclose(fp);

	return err;
}