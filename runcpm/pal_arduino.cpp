#include "utils.h"
#include "defaults.h"
#include "globals.h"
#include "pal.h"
#include "ram.h"
#include "disk.h"

#include <SD.h>

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <unistd.h>

uint8_t _pal_ram_load(uint8_t *filename, uint16_t address) {
	File f;
	uint8_t result = 1;

	if (f = SD.open((const char*)filename, FILE_READ)) {
		while (f.available()) {
			_ram_write(address++, f.read());
        }
		f.close();
		result = 0;
	}
	return(result);
}

File root;

int _pal_select(uint8_t *disk) {
	uint8_t result = 0;
	File f;

	digitalWrite(EMULATOR_LED, HIGH);
	if (f = SD.open((char *)disk, O_READ)) {
		if (f.isDirectory())
			result = 1;
		f.close();
	}
	digitalWrite(EMULATOR_LED, LOW);
	return(result);
}

long _pal_file_size(uint8_t *filename) {
	long l = -1;
	File f;

	digitalWrite(EMULATOR_LED, HIGH);
	if (f = SD.open((char *)filename, O_RDONLY)) {
		l = f.size();
		f.close();
	}
	digitalWrite(EMULATOR_LED, LOW);
	return(l);
}

int _pal_open_file(uint8_t *filename) {
	File f;
	int result = 0;

	digitalWrite(EMULATOR_LED, HIGH);
	f = SD.open((char *)filename, O_READ);
	if (f) {
		f.close();
		result = 1;
	}
	digitalWrite(EMULATOR_LED, LOW);
	return(result);
}

int _pal_make_file(uint8_t *filename) {
	File f;
	int result = 0;

	digitalWrite(EMULATOR_LED, HIGH);
	f = SD.open((char *)filename, O_CREAT | O_WRITE);
	if (f) {
		f.close();
		result = 1;
	}
	digitalWrite(EMULATOR_LED, LOW);
	return(result);
}

int _pal_delete_file(uint8_t *filename) {
	digitalWrite(EMULATOR_LED, HIGH);
	return(SD.remove((char *)filename));
	digitalWrite(EMULATOR_LED, LOW);
}

int _pal_move_file(char *filename, char *newname, int size) {
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
	return(result);
}

int _pal_rename_file(uint8_t *filename, uint8_t *newname) {
	return _pal_move_file((char *)filename, (char *)newname, _pal_file_size(filename));
}

#ifdef DEBUG_LOG
void _pal_log_buffer(uint8_t *buffer) {
#ifdef DEBUG_LOG_TO_CONSOLE
	puts((char *)buffer);
#else
	File f;
	uint8_t s = 0;
	while (*(buffer+s))	// Computes buffer size
		s++;
	if(f = SD.open(DEBUG_LOG_PATH, O_CREAT | O_APPEND | O_WRITE)) {
		f.write(buffer, s);
		f.flush();
		f.close();
	}
#endif
}
#endif

uint8_t _pal_extend_file(char *fn, unsigned long fpos)
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
	return(result);
}

uint8_t _pal_read_seq(uint8_t *filename, long fpos) {
	uint8_t result = 0xff;
	File f;
	uint8_t bytesread;
	uint8_t dmabuf[128];
	uint8_t i;

	digitalWrite(EMULATOR_LED, HIGH);
	if (!_pal_extend_file((char*)filename, fpos))
		f = SD.open((char*)filename, O_READ);
	if (f) {
		if (f.seek(fpos)) {
			for (i = 0; i < 128; i++)
				dmabuf[i] = 0x1a;
			bytesread = f.read(&dmabuf[0], 128);
			if (bytesread) {
				for (i = 0; i < 128; i++)
					_ram_write(_glb_dma_addr + i, dmabuf[i]);
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
	return(result);
}

uint8_t _pal_write_seq(uint8_t *filename, long fpos) {
	uint8_t result = 0xff;
	File f;

	digitalWrite(EMULATOR_LED, HIGH);
	if (!_pal_extend_file((char*)filename, fpos))
		f = SD.open((char*)filename, O_RDWR);
	if (f) {
		if (f.seek(fpos)) {
       for(int i=0;i<128;i++) {
          // TODO
          f.write(_ram_read(_glb_dma_addr+i));
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
	return(result);
}

uint8_t _pal_read_rand(uint8_t *filename, long fpos) {
	uint8_t result = 0xff;
	File f;
	uint8_t bytesread;
	uint8_t dmabuf[128];
	uint8_t i;

	digitalWrite(EMULATOR_LED, HIGH);
	if (!_pal_extend_file((char*)filename, fpos))
		f = SD.open((char*)filename, O_READ);
	if (f) {
		if (f.seek(fpos)) {
			for (i = 0; i < 128; i++)
				dmabuf[i] = 0x1a;
			bytesread = f.read(&dmabuf[0], 128);
			if (bytesread) {
				for (i = 0; i < 128; i++)
					_ram_write(_glb_dma_addr + i, dmabuf[i]);
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
	return(result);
}

uint8_t _pal_write_rand(uint8_t *filename, long fpos) {
	uint8_t result = 0xff;
	File f;

	digitalWrite(EMULATOR_LED, HIGH);
	if (!_pal_extend_file((char*)filename, fpos)) {
		f = SD.open((char*)filename, O_RDWR);
	}
	if (f) {
		if (f.seek(fpos)) {
       for(int i=0;i<128;i++) {
          // TODO
          f.write(_ram_read(_glb_dma_addr+i));
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
	return(result);
}

uint8_t _pal_find_next(uint8_t isdir) {
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
		_fcb_hostname_to_fcbname(dirname, _glb_fcb_name);
		if (_pal_file_match(_glb_fcb_name, _glb_pattern)) {
			if (isdir) {
				_fcb_hostname_to_fcb(_glb_dma_addr, dirname);
				_ram_write(_glb_dma_addr, 0x00);
			}
			_ram_write(GLB_TMP_FCB_ADDR, _glb_file_name[0] - '@');
			_fcb_hostname_to_fcb(GLB_TMP_FCB_ADDR, dirname);
			result = 0x00;
			break;
		}
	}
	digitalWrite(EMULATOR_LED, LOW);
	return(result);
}

uint8_t _pal_find_first(uint8_t isdir) {
#ifdef USER_SUPPORT
	uint8_t path[4] = { '?', FOLDER_SEP, '?', 0 };
#else
	uint8_t path[2] = { '?', 0 };
#endif
	path[0] = _glb_file_name[0];
#ifdef USER_SUPPORT
	path[2] = _glb_file_name[2];
#endif
	if (root)
		root.close();
	root = SD.open((char *)path); // Set directory search to start from the first position
	_fcb_hostname_to_fcbname(_glb_file_name, _glb_pattern);
	return(_pal_find_next(isdir));
}

uint8_t _pal_truncate(char *filename, uint8_t rc) {
	uint8_t result = 0xff;
	File f;

	if (_pal_move_file(filename, (char *)"$$$$$$$$.$$$", _pal_file_size((uint8_t *)filename))) {
		if (_pal_move_file((char *)"$$$$$$$$.$$$", filename, rc * 128)) {
			result = 0x00;
		}
	}
	return(result);
}

#ifdef USER_SUPPORT
void _pal_make_user_dir() {
	uint8_t d_folder = _glb_c_drive + 'A';
	uint8_t u_folder = toupper(tohex(_glb_user_code));

	uint8_t path[4] = { d_folder, FOLDER_SEP, u_folder, 0 };

	SD.mkdir((char*)path);
}
#endif

#define SDELAY 50
void _pal_console_init(void) {
    Serial.begin(9600);
	while (!Serial) {	// Wait until serial is connected
		digitalWrite(EMULATOR_LED, HIGH);
		delay(SDELAY);
		digitalWrite(EMULATOR_LED, LOW);
		delay(SDELAY);
	}
}

void _pale_console_reset(void) {

}

int _pal_kbhit(void) {
	return(Serial.available());
}

uint8_t _pal_getch(void) {
	while (!Serial.available());
	return(Serial.read());
}

uint8_t _pal_getche(void) {
	uint8_t ch = _pal_getch();
	Serial.write(ch);
	return(ch);
}

void _pal_putch(uint8_t ch) {
	Serial.write(ch);
}

void _pal_clrscr(void) {
	Serial.println("\e[H\e[J");
}
