#include "defaults.h"
#include "globals.h"
#include "pal.h"
#include "ram.h"
#include "disk.h"

#include <Arduino.h>
#include <SD.h>

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <unistd.h>

#define TO_HEX(x)   (x < 10 ? x + 48 : x + 87)

uint8_t pal_init() {
    pinMode(EMULATOR_LED, OUTPUT);
	digitalWrite(EMULATOR_LED, LOW);
    return SD.begin(SD_SPI_CS)?1:0;
}

void pal_digital_set(uint16_t ind, uint16_t state) {
    digitalWrite(ind, state);
}

void pal_pin_set_mode(uint16_t pin, uint16_t mode) {
    pinMode(pin,mode);
}

uint16_t pal_digital_get(uint16_t ind) {
    return digitalRead(ind);
}

void pal_analog_set(uint16_t ind, uint16_t state) {
    analogWrite(ind, state);
}

uint16_t pal_analog_get(uint16_t ind) {
    return analogRead(ind);
}

uint8_t pal_load_file(uint8_t *filename, uint16_t address) {
	File f;
	uint8_t result = 1;

	if (f = SD.open((const char*)filename, FILE_READ)) {
		while (f.available()) {
			ram_write(address++, f.read());
		}
		f.close();
		result = 0;
	}
	return (result);
}

File root;

uint8_t pal_file_exists(uint8_t *filename) {
	return SD.exists((char*)filename);
}


int pal_select(uint8_t *disk) {
	uint8_t result = 0;
	File f;

	digitalWrite(EMULATOR_LED, HIGH);
	if (f = SD.open((char *)disk, O_READ)) {
		if (f.isDirectory())
			result = 1;
		f.close();
	}
	digitalWrite(EMULATOR_LED, LOW);
	return (result);
}

long pal_file_size(uint8_t *filename) {
	long l = -1;
	File f;

	digitalWrite(EMULATOR_LED, HIGH);
	if (f = SD.open((char *)filename, O_RDONLY)) {
		l = f.size();
		f.close();
	}
	digitalWrite(EMULATOR_LED, LOW);
	return (l);
}

int pal_open_file(uint8_t *filename) {
	File f;
	int result = 0;

	digitalWrite(EMULATOR_LED, HIGH);
	f = SD.open((char *)filename, O_READ);
	if (f) {
		f.close();
		result = 1;
	}
	digitalWrite(EMULATOR_LED, LOW);
	return (result);
}

int pal_make_file(uint8_t *filename) {
	File f;
	int result = 0;

	digitalWrite(EMULATOR_LED, HIGH);
	f = SD.open((char *)filename, O_CREAT | O_WRITE);
	if (f) {
		f.close();
		result = 1;
	}
	digitalWrite(EMULATOR_LED, LOW);
	return (result);
}

int pal_delete_file(uint8_t *filename) {
	digitalWrite(EMULATOR_LED, HIGH);
	return (SD.remove((char *)filename));
	digitalWrite(EMULATOR_LED, LOW);
}

int pal_move_file(char *filename, char *newname, int size) {
	File fold, fnew;
	int i, result = false;
	uint8_t c;

	digitalWrite(EMULATOR_LED, HIGH);
	if (fold = SD.open(filename, O_READ)) {
		if (fnew = SD.open(newname, O_CREAT | O_WRITE)) {
			result = true;
			for (i = 0; i < size; i++) {
				c = fold.read();
				if (fnew.write(c) < 1) {
					result = false;
					break;
				}
			}
			fnew.close();
		}
		fold.close();
	}
	if (result)
		SD.remove(filename);
	else
		SD.remove(newname);
	digitalWrite(EMULATOR_LED, LOW);
	return (result);
}

int pal_rename_file(uint8_t *filename, uint8_t *newname) {
	return pal_move_file((char *)filename, (char *)newname, pal_file_size(filename));
}

#ifdef DEBUG_LOG
void pal_log_buffer(uint8_t *buffer) {
#ifdef DEBUG_LOG_TO_CONSOLE
	puts((char *)buffer);
#else
	File f;
	uint8_t s = 0;
	while (*(buffer + s)) // Computes buffer size
		s++;
	if (f = SD.open(DEBUG_LOG_PATH, O_CREAT | O_APPEND | O_WRITE)) {
		f.write(buffer, s);
		f.flush();
		f.close();
	}
#endif
}
#endif

uint8_t pal_extend_file(char *fn, unsigned long fpos)
{
	uint8_t result = 0;
	File f;
	unsigned long i;

	digitalWrite(EMULATOR_LED, HIGH);
	if (f = SD.open(fn, O_WRITE | O_APPEND)) {
		if (fpos > f.size()) {
			for (i = 0; i < f.size() - fpos; i++) {
				if (f.write((uint8_t)0) < 0) {
					result = false;
					break;
				}
			}
		}
		f.close();
	} else {
		result = 1;
	}
	digitalWrite(EMULATOR_LED, LOW);
	return (result);
}

uint8_t pal_read_seq(uint8_t *filename, long fpos) {
	uint8_t result = 0xff;
	File f;
	uint8_t bytesread;
	uint8_t dmabuf[128];
	uint8_t i;

	digitalWrite(EMULATOR_LED, HIGH);
	if (!pal_extend_file((char*)filename, fpos))
		f = SD.open((char*)filename, O_READ);
	if (f) {
		if (f.seek(fpos)) {
			for (i = 0; i < 128; i++)
				dmabuf[i] = 0x1a;
			bytesread = f.read(&dmabuf[0], 128);
			if (bytesread) {
				for (i = 0; i < 128; i++)
					ram_write(glb_dma_addr + i, dmabuf[i]);
			}
			result = bytesread ? 0x00 : 0x01;
		} else {
			result = 0x01;
		}
		f.close();
	} else {
		result = 0x10;
	}
	digitalWrite(EMULATOR_LED, LOW);
	return (result);
}

uint8_t pal_write_seq(uint8_t *filename, long fpos) {
	uint8_t result = 0xff;
	File f;

	digitalWrite(EMULATOR_LED, HIGH);
	if (!pal_extend_file((char*)filename, fpos))
		f = SD.open((char*)filename, O_RDWR);
	if (f) {
		if (f.seek(fpos)) {
			for (int i = 0; i < 128; i++) {
				// TODO
				f.write(ram_read(glb_dma_addr + i));
			}
			result = 0x00;

		} else {
			result = 0x01;
		}
		f.close();
	} else {
		result = 0x10;
	}
	digitalWrite(EMULATOR_LED, LOW);
	return (result);
}

uint8_t pal_read_rand(uint8_t *filename, long fpos) {
	uint8_t result = 0xff;
	File f;
	uint8_t bytesread;
	uint8_t dmabuf[128];
	uint8_t i;

	digitalWrite(EMULATOR_LED, HIGH);
	if (!pal_extend_file((char*)filename, fpos))
		f = SD.open((char*)filename, O_READ);
	if (f) {
		if (f.seek(fpos)) {
			for (i = 0; i < 128; i++)
				dmabuf[i] = 0x1a;
			bytesread = f.read(&dmabuf[0], 128);
			if (bytesread) {
				for (i = 0; i < 128; i++)
					ram_write(glb_dma_addr + i, dmabuf[i]);
			}
			result = bytesread ? 0x00 : 0x01;
		} else {
			result = 0x06;
		}
		f.close();
	} else {
		result = 0x10;
	}
	digitalWrite(EMULATOR_LED, LOW);
	return (result);
}

uint8_t pal_write_rand(uint8_t *filename, long fpos) {
	uint8_t result = 0xff;
	File f;

	digitalWrite(EMULATOR_LED, HIGH);
	if (!pal_extend_file((char*)filename, fpos)) {
		f = SD.open((char*)filename, O_RDWR);
	}
	if (f) {
		if (f.seek(fpos)) {
			for (int i = 0; i < 128; i++) {
				// TODO
				f.write(ram_read(glb_dma_addr + i));
			}
			result = 0x00;
		} else {
			result = 0x06;
		}
		f.close();
	} else {
		result = 0x10;
	}
	digitalWrite(EMULATOR_LED, LOW);
	return (result);
}

uint8_t pal_find_next(uint8_t isdir) {
	File f;
	uint8_t result = 0xff;
	uint8_t dirname[13];
	char* fname;
	uint8_t isfile;
	unsigned int i;

	digitalWrite(EMULATOR_LED, HIGH);
	while (f = root.openNextFile()) {
		fname = f.name();
		for (i = 0; i < strlen(fname) + 1 && i < 13; i++)
			dirname[i] = fname[i];
		isfile = !f.isDirectory();
		f.close();
		if (!isfile)
			continue;
		fcb_hostname_to_fcbname(dirname, glb_fcb_name);
		if (pal_file_match(glb_fcb_name, glb_pattern)) {
			if (isdir) {
				fcb_hostname_to_fcb(glb_dma_addr, dirname);
				ram_write(glb_dma_addr, 0x00);
			}
			ram_write(GLB_TMP_FCB_ADDR, glb_file_name[0] - '@');
			fcb_hostname_to_fcb(GLB_TMP_FCB_ADDR, dirname);
			result = 0x00;
			break;
		}
	}
	digitalWrite(EMULATOR_LED, LOW);
	return (result);
}

uint8_t pal_find_first(uint8_t isdir) {
#ifdef USER_SUPPORT
	uint8_t path[4] = { '?', FOLDER_SEP, '?', 0 };
#else
	uint8_t path[2] = { '?', 0 };
#endif
	path[0] = glb_file_name[0];
#ifdef USER_SUPPORT
	path[2] = glb_file_name[2];
#endif
	if (root)
		root.close();
	root = SD.open((char *)path); // Set directory search to start from the first position
	fcb_hostname_to_fcbname(glb_file_name, glb_pattern);
	return (pal_find_next(isdir));
}

uint8_t pal_truncate(char *filename, uint8_t rc) {
	uint8_t result = 0xff;
	File f;

	if (pal_move_file(filename, (char *)"$$$$$$$$.$$$", pal_file_size((uint8_t *)filename))) {
		if (pal_move_file((char *)"$$$$$$$$.$$$", filename, rc * 128)) {
			result = 0x00;
		}
	}
	return (result);
}

#ifdef EMULATOR_USER_SUPPORT
void pal_make_user_dir() {
	uint8_t d_folder = glb_c_drive + 'A';
	uint8_t u_folder = toupper(TO_HEX(glb_user_code));

	uint8_t path[4] = { d_folder, GLB_FOLDER_SEP, u_folder, 0 };

	SD.mkdir((char*)path);
}
#endif

#define SDELAY 50
void pal_console_init(void) {
	Serial.begin(9600);
	while (!Serial) { // Wait until serial is connected
		digitalWrite(EMULATOR_LED, HIGH);
		delay(SDELAY);
		digitalWrite(EMULATOR_LED, LOW);
		delay(SDELAY);
	}
}

void pal_console_reset(void) {

}

int pal_kbhit(void) {
	return (Serial.available());
}

uint8_t pal_getch(void) {
	while (!Serial.available());
	return (Serial.read());
}

uint8_t pal_getche(void) {
	uint8_t ch = pal_getch();
	Serial.write(ch);
	return (ch);
}

void pal_putch(uint8_t ch) {
	Serial.write(ch);
}

void pal_clrscr(void) {
	Serial.println("\e[H\e[J");
}
