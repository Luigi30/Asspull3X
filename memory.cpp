#include "asspull.h"

extern void SendMidi(unsigned int message);

extern "C"
{
unsigned int  m68k_read_memory_8(unsigned int address);
unsigned int  m68k_read_memory_16(unsigned int address);
unsigned int  m68k_read_memory_32(unsigned int address);
void m68k_write_memory_8(unsigned int address, unsigned int value);
void m68k_write_memory_16(unsigned int address, unsigned int value);
void m68k_write_memory_32(unsigned int address, unsigned int value);
}

unsigned char* romBIOS = NULL;
unsigned char* romCartridge = NULL;
unsigned char* ramInternal = NULL;
unsigned char* ramVideo = NULL;

unsigned int biosSize = 0, romSize = 0;

int keyScan, joypad[2];

int dmaSource, dmaTarget;
unsigned int dmaLength;
bool hdmaOn[8], hdmaDouble[8];
int hdmaSource[8], hdmaTarget[8], hdmaWidth[8], hdmaStart[8], hdmaCount[8];

void HandleBlitter(unsigned int function);
unsigned int blitLength;
int blitAddrA, blitAddrB, blitKey;

extern unsigned char PollKeyboard(bool force);

long ticks = 0;

extern bool gfx320, gfx240, gfxTextBold;
extern int gfxMode, gfxFade, scrollX[4], scrollY[4], tileShift[4], mapEnabled[4], mapBlend[4];

extern int line, interrupts;

void HandleHdma(int line)
{
	for (auto i = 0; i < 8; i++)
	{
		if (!hdmaOn[i])
			continue;
		if (line < hdmaStart[i])
			continue;
		if (line > hdmaStart[i] + hdmaCount[i])
			continue;
		line -= hdmaStart[i];
		auto l = line / (hdmaDouble[i] ? 2 : 1);
		auto width = hdmaWidth[i];
		auto target = hdmaTarget[i];
		auto source = hdmaSource[i];
		switch (width)
		{
			case 0:
				source += l;
				m68k_write_memory_8(target, m68k_read_memory_8(source));
				break;
			case 1:
				source += l * 2;
				m68k_write_memory_16(target, m68k_read_memory_16(source));
				break;
			case 2:
				source += l * 4;
				m68k_write_memory_32(target, m68k_read_memory_32(source));
				break;
		}
	}
}

int InitMemory()
{
	if ((romBIOS = (unsigned char*)malloc(BIOS_SIZE)) == NULL) return -1;
	SDL_memset(romBIOS, 0, BIOS_SIZE);
	if ((romCartridge = (unsigned char*)malloc(CART_SIZE)) == NULL) return -1;
	SDL_memset(romCartridge, 0, CART_SIZE);
	if ((ramInternal = (unsigned char*)malloc(WRAM_SIZE)) == NULL) return -1;
	SDL_memset(ramInternal, 0, WRAM_SIZE);
	if ((ramVideo = (unsigned char*)malloc(VRAM_SIZE)) == NULL) return -1;
	SDL_memset(ramVideo, 0, VRAM_SIZE);
	return 0;
}

unsigned int m68k_read_memory_8(unsigned int address)
{
	if (address >= REGS_ADDR && address < REGS_ADDR + REGS_SIZE)
	{
		auto reg = address & REGS_SIZE;
		switch (reg)
		{
			case 0x03: //Screen Mode
				return (gfxMode |
					(gfx240 ? 1 << 5 : 0) |
					(gfx320 ? 1 << 6 : 0) |
					(gfxTextBold ? 1 << 7 : 0));
			case 0x04: //ScreenFade
				return gfxFade;
			case 0x05: //Interrupts
				return interrupts;
			case 0x0C: //Joypad
			case 0x0D:
				return joypad[reg - 0x0C];
			case 0x0F: //TilemapSet
				return (tileShift[1] |
					(tileShift[0] << 2) |
					(mapEnabled[1] << 6) |
					(mapEnabled[0] << 7));
				//TODO: add mapEnabled[2] and [3].
		}
		return 0;
	}
	auto bank = (address & 0x0F000000) >> 24;
	auto addr = address & 0x00FFFFFF;
	switch (bank)
	{
		case 0x0:
			if (addr < BIOS_SIZE)
				return romBIOS[addr & (biosSize - 1)];
			return romCartridge[(addr - CART_ADDR) & (romSize - 1)];
		case 0x1:
			return ramInternal[addr & (WRAM_SIZE - 1)];
		case 0x2: //DEV
			{
				auto devnum = addr / DEVBLOCK;
				if (devices[devnum] != NULL)
					return devices[devnum]->Read(addr % DEVBLOCK);
				return 0;
			}
			break;
		case 0xE:
			addr &= 0x7FFFF;
			if (addr >= VRAM_SIZE)
				return 0;
			return ramVideo[addr];
	}
	return 0;
}

unsigned int m68k_read_memory_16(unsigned int address)
{
	if (address >= REGS_ADDR && address < REGS_ADDR + REGS_SIZE)
	{
		auto reg = address & 0x000FFFFF;
		switch (reg)
		{
			case 0x00: //Line
				return line;
			case 0x06: //Keyscan
				keyScan = PollKeyboard(false);
				return keyScan;
			case 0x10: //Horizontal scroll
			case 0x14:
			case 0x18:
			case 0x1C:
				return scrollX[(reg - 0x10) / 2];
				break;
			case 0x12: //Vertical scroll
			case 0x16:
			case 0x1A:
			case 0x1E:
				return scrollY[(reg - 0x12) / 2];
				break;
		}
		return 0;
	}
	auto ret = m68k_read_memory_8(address) * 0x100;
	ret += m68k_read_memory_8(address + 1);
	return ret;
}

unsigned int m68k_read_memory_32(unsigned int address)
{
	if (address >= REGS_ADDR && address < REGS_ADDR + REGS_SIZE)
	{
		auto reg = address & 0x000FFFFF;
		switch (reg)
		{
			case 0x08: //Ticks
				return (int)ticks;
			case 0x20: //DMA Source
				return dmaSource;
			case 0x24: //DMA Target
				return dmaTarget;
			case 0x28: //DMA Length
				return dmaLength;
			//TODO: HDMA. Fun!
		}
		return 0;
	}
	auto ret = m68k_read_memory_8(address) * 0x1000000;
	ret += m68k_read_memory_8(address + 1) * 0x10000;
	ret += m68k_read_memory_8(address + 2) * 0x100;
	ret += m68k_read_memory_8(address + 3) * 0x1;
	return ret;
}

void m68k_write_memory_8(unsigned int address, unsigned int value)
{
	if (address >= REGS_ADDR && address < REGS_ADDR + REGS_SIZE)
	{
		auto reg = address & 0x000FFFFF;
		auto u8 = (unsigned char)value;
		switch (reg)
		{
			case 0x02: //TilemapBlend
				mapBlend[0] = ((u8 >> 0) & 1) | (((u8 >> 4) & 1) << 1);
				mapBlend[1] = ((u8 >> 1) & 1) | (((u8 >> 5) & 1) << 1);
				mapBlend[2] = ((u8 >> 2) & 1) | (((u8 >> 6) & 1) << 1);
				mapBlend[3] = ((u8 >> 3) & 1) | (((u8 >> 7) & 1) << 1);
				break;
			case 0x03: //ScreenMode
				gfxTextBold = ((u8 >> 7) & 1) == 1;
				gfx320 = ((u8 >> 6) & 1) == 1;
				gfx240 = ((u8 >> 5) & 1) == 1;
				gfxMode = u8 & 0x0F;
				break;
			case 0x04: //ScreenFade
				gfxFade = u8;
				break;
			case 0x5: //VBlankMode
				interrupts = value;
				break;
			case 0x0E: //Debug
				printf("%c", (char)value);
				break;
			case 0x0F: //TilemapSet
				mapEnabled[0] = (value & 0x10);
				mapEnabled[1] = (value & 0x20);
				mapEnabled[2] = (value & 0x40);
				mapEnabled[3] = (value & 0x80);
				tileShift[0] = (value >> 2) & 3;
				tileShift[1] = value & 3;
				break;
			case 0x2A: //DMA Control
				{
				if ((value & 1) == 0) return;
				auto increaseSource = ((value >> 1) & 1) == 1;
				auto increaseTarget = ((value >> 2) & 1) == 1;
				auto asValue = ((value >> 3) & 1) == 1;
				auto dataWidth = (value >> 4) & 3;
				auto increaseStep = (dataWidth == 0) ? 1 : (dataWidth == 1) ? 2 : 4;
				if (asValue && dataWidth == 2 && dmaTarget >= 0x1000000 && dmaTarget < 0xA000000)
				{
					//Do it quicker!
					//TODO: allow this for all long-sized transfers that aren't into register space?
					//SDL_Log("DMA: detected a long-width memset into WRAM.");
					SDL_memset((void*)&ramInternal[dmaTarget - 0x1000000], dmaSource, dmaLength);
					break;
				}
				while (dmaLength > 0)
				{
					if (asValue)
					{
						if (dataWidth == 0) m68k_write_memory_8(dmaTarget, dmaSource);
						else if (dataWidth == 1) m68k_write_memory_16(dmaTarget, dmaSource);
						else if (dataWidth == 2) m68k_write_memory_32(dmaTarget, dmaSource);
					}
					else
					{
						if (dataWidth == 0) m68k_write_memory_8(dmaTarget, m68k_read_memory_8(dmaSource));
						else if (dataWidth == 1) m68k_write_memory_16(dmaTarget, m68k_read_memory_16(dmaSource));
						else if (dataWidth == 2) m68k_write_memory_32(dmaTarget, m68k_read_memory_32(dmaSource));
					}
					dmaLength--;
					if (increaseSource) dmaSource += increaseStep;
					if (increaseTarget) dmaTarget += increaseStep;
				}
				break;
				}
			case 0x110: //Blitter key
				blitKey = value;
				break;
		}
		return;
	}
	auto bank = (address & 0x0F000000) >> 24;
	auto addr = address & 0x00FFFFFF;
	switch (bank)
	{
		case 0x0: /* BIOS is ROM */ break;
		case 0x1:
			if (addr < WRAM_SIZE)
				ramInternal[addr & (WRAM_SIZE - 1)] = (unsigned char)value;
			break;
		case 0x2: //DEV
			{
				auto devnum = addr / DEVBLOCK;
				if (devices[devnum] != NULL)
					devices[devnum]->Write(addr % DEVBLOCK, value);
			}
			break;
		case 0xE:
			addr &= 0x7FFFF;
			if (addr >= VRAM_SIZE)
				break;
			ramVideo[addr] = (unsigned char)value; break;
	}
}

void m68k_write_memory_16(unsigned int address, unsigned int value)
{
	if (address >= REGS_ADDR && address < REGS_ADDR + REGS_SIZE)
	{
		auto reg = address & 0x000FFFFF;
		switch (reg)
		{
			case 0x10: //Horizontal scroll
			case 0x14:
			case 0x18:
			case 0x1C:
				scrollX[(reg - 0x10) / 4] = value & 511;
				break;
			case 0x12: //Vertical scroll
			case 0x16:
			case 0x1A:
			case 0x1E:
				scrollY[(reg - 0x12) / 4] = value & 511;
				break;
		}
		return;
	}
	m68k_write_memory_8(address + 0, (value >> 8));
	m68k_write_memory_8(address + 1, (value >> 0));
}

void m68k_write_memory_32(unsigned int address, unsigned int value)
{
	if (address >= REGS_ADDR && address < REGS_ADDR + REGS_SIZE)
	{
		auto reg = address & 0x000FFFFF;
		switch (reg)
		{
			case 0x20: //DMA Source
				dmaSource = value;
				break;
			case 0x24: //DMA Target
				dmaTarget = value;
				break;
			case 0x28: //DMA Length
				dmaLength = value;
				break;
			case 0x40: //MIDI Out
				if (value > 0)
					SendMidi(value);
				break;
			case 0x80: //HDMA Control
			case 0x84:
			case 0x88:
			case 0x8C:
			case 0x90:
			case 0x94:
			case 0x98:
			case 0x9C:
				{
				auto channel = (reg & 0xF) / 4;
				hdmaWidth[channel] = (value >> 4) & 3;
				hdmaDouble[channel] = ((value >> 7) & 1) == 1;
				hdmaOn[channel] = (value & 1) == 1;
				hdmaStart[channel] = (value >> 8) & 0x3FF;
				hdmaCount[channel] = (value >> 20) & 0x3FF;
				break;
				}
			case 0xA0: //HDMA Source
			case 0xA4:
			case 0xA8:
			case 0xAC:
			case 0xB0:
			case 0xB4:
			case 0xB8:
			case 0xBC:
				{
				auto channel = (reg & 0xF) / 4;
				hdmaSource[channel] = value;
				break;
				}
			case 0xC0: //HDMA Target
			case 0xC4:
			case 0xC8:
			case 0xCC:
			case 0xD0:
			case 0xD4:
			case 0xD8:
			case 0xDC:
				{
				auto channel = (reg & 0xF) / 4;
				hdmaTarget[channel] = value;
				break;
				}

			case 0x100: //Blitter function
				HandleBlitter(value);
				break;
			case 0x104: //Blitter address A
				blitAddrA = value;
				break;
			case 0x108: //Blitter address B
				blitAddrB = value;
				break;
			case 0x10C: //Blitter length
				blitLength = value;
				break;
		}
		return;
	}
	m68k_write_memory_8(address + 0, (value >> 24));
	m68k_write_memory_8(address + 1, (value >> 16));
	m68k_write_memory_8(address + 2, (value >> 8));
	m68k_write_memory_8(address + 3, (value >> 0));
}

void HandleBlitter(unsigned int function)
{
	auto fun = function & 0xF;
	switch (fun)
	{
		case 0: return;
		case 1: //Blit
		case 2: //Set
		case 3: //Invert
			{
				auto strideSkip = ((function & 0x10) >> 4) == 1; //1 2 3
				auto colorKey = ((function & 0x20) >> 5) == 1; //1
				auto width = ((function & 0x80) >> 6); //2
				auto sourceStride = ((function >> 8) & 0xFFF); //1 2 3
				auto targetStride = ((function >> 20) & 0xFFF); //1 2 3

				auto read = m68k_read_memory_8;
				if (width == 1) read = m68k_read_memory_16;
				else if (width == 2) read = m68k_read_memory_32;
				auto write = m68k_write_memory_8;
				if (width == 1) write = m68k_write_memory_16;
				else if (width == 2) write = m68k_write_memory_32;

				int val = 0;

				if (fun == 1) //Blit
				{
					while (blitLength > 0)
					{
						if (strideSkip)
						{
							for (unsigned int i = 0; i < sourceStride && blitLength > 0; i++, blitAddrA += (1 << width), blitAddrB += (1 << width), blitLength--)
							{
								val = read(blitAddrA);
								if (colorKey && val == blitKey) continue;
								write(blitAddrB, val);
							}
							blitAddrB += (int)(targetStride - sourceStride) << width;
						}
						else
						{
							val = read(blitAddrA);
							if (colorKey && val == blitKey) continue;
							write(blitAddrB, val);
							blitAddrA += (1 << width);
							blitAddrB += (1 << width);
							blitLength--;
						}
					}
				}
				else if (fun == 2) //Set
				{
					/*
					Copies the value of ADDRESS A to B.
					If STRIDESKIP is enabled, copies SOURCE STRIDE bytes,
					then skips over TARGET STRIDE - SOURCE STRIDE bytes,
					until LENGTH bytes are copied in total.
					If WIDTH is set to 0, sets B to the low byte of the source value.
					If WIDTH is set to 1, sets B to the lower short instead.
					If WIDTH is set to 2, sets B to the full word.
					If WIDTH is set to 3, behavior is undefined.
					*/
					while (blitLength > 0)
					{
						if (strideSkip)
						{
							for (unsigned int i = 0; i < sourceStride && blitLength > 0; i++, blitAddrB += (1 << width), blitLength--)
								write(blitAddrB, blitAddrA);
							blitAddrB += (int)(targetStride - sourceStride) << width;
						}
						else
						{
							write(blitAddrB, read(blitAddrB));
							blitAddrB += (1 << width);
							blitLength--;
						}
					}
				}
				else if (fun == 3) //Invert
				{
					while (blitLength > 0)
					{
						if (strideSkip)
						{
							for (unsigned int i = 0; i < sourceStride && blitLength > 0; i++, blitAddrB++, blitLength--)
								m68k_write_memory_8(blitAddrB, ~m68k_read_memory_8(blitAddrB));
							blitAddrB += (int)(targetStride - sourceStride);
						}
						else
						{
							m68k_write_memory_8(blitAddrB, ~m68k_read_memory_8(blitAddrB));
							blitAddrB++;
							blitLength--;
						}
					}
				}
			}
			break;
		case 4: //UnRLE
			unsigned int i = 0;
			unsigned char data = 0;
			while (blitLength) //hack
			{
				data = m68k_read_memory_8(blitAddrA++);
				if ((data & 0xC0) == 0xC0)
				{
					auto len = data & 0x3F;
					data = m68k_read_memory_8(blitAddrA++);
					blitLength--;
					if (data == 0xC0 && len == 0)
						break;
					for (; len > 0; len--)
						m68k_write_memory_8(blitAddrB++, data);
				}
				else
				{
					m68k_write_memory_8(blitAddrB++, data);
				}
				blitLength--;
			}
			break;
	}
}
