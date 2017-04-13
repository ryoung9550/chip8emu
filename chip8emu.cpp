#include <SDL2/SDL.h>
#include <SDLShapes.hpp>
#include <cstdint>
#include <vector>
#include <array>
#include <memory>
#include <time.h>

#define SCALE 11
typedef uint8_t u8;
typedef uint16_t u16;

template<typename T = u8, int size = 4096>
class Memory {
	std::array<T, size> memory; // 4K of Memory
public:
	Memory() { memory = {}; }
	u8 RB(u16 addr) { return (u8 &)memory[addr]; } // Read data from address
	void WB(u16 addr, const u8 & value) { memory[addr] = value; } // Write to address
};

class Stack : public std::vector<u16> {
public:
	void pop(u16 &pc) {
		pc = this->back();
		this->pop_back();
	}
};

struct SDL_Deleter {
	void operator()(SDL_Surface* ptr) { SDL_FreeSurface(ptr); }
	void operator()(SDL_Window* ptr) { SDL_DestroyWindow(ptr); }
};

class Display {
	std::unique_ptr<SDL_Window, SDL_Deleter> win;
	std::unique_ptr<SDL_Surface> surfBuffer;
	std::unique_ptr<SDL_Surface> winSurface;
	std::array<u8, 256> screenPixels{};
	/* Screen Resolution of the chip8 is 64(h) x 32(v)
	* Each row consists of 8 bytes of data. So to get
	* to the next row the height value multiplied by
	* the 8 to adjust for the correct height
	*/
public:
	Display() {
		SDL_Init(SDL_INIT_EVERYTHING);
		win.reset(SDL_CreateWindow(
			"Chip8 Interpreter", // Title
			SDL_WINDOWPOS_UNDEFINED, // Window Start X
			SDL_WINDOWPOS_UNDEFINED, // Window Start Y
			64 * SCALE, // Window width
			32 * SCALE, // Window height
			0)); // Window Flags 
		winSurface.reset(SDL_GetWindowSurface(win.get()));
		surfBuffer.reset(SDL_ConvertSurface(winSurface.get(), winSurface.get()->format, 0));
	}
	/*
	~Display() {
	win.reset();
	winSurface.reset();
	surfBuffer.reset();
	}
	*/

	bool isInit() {
		if (win.get() == nullptr) { printf("Window did not initialize!\n"); return false; }
		if (winSurface.get() == nullptr) { printf("Surface did not initialize!\n"); return false; }
		if (surfBuffer.get() == nullptr) { printf("Surface did not initialize!\n"); return false; }
		return true;
	}

	void clear() {
		for (int i = 0; i < 256; ++i) {
			screenPixels[i] = 0;
		}
	}

	bool predrawSurf(const u16 & addr, Memory<u8> & RAM, const u8 & nBytes, const u8 & x, const u8 & y) {
		bool collision = false;
		unsigned xOffset = (x % 8);
		unsigned xByte = x / 8;


		if /*constexpr*/ (xOffset == 0) { // If the sprite is alligned with memory
			for (int i = 0; i < nBytes; ++i) {
				//unsigned yAdj = (y + i) % 32;
				unsigned yAdj = (y * 8 + i * 8) % 256;
				u8 nextByte = RAM.RB(addr + i);
				unsigned arrayInx = xByte + yAdj;
				screenPixels[arrayInx] = nextByte ^ screenPixels[arrayInx];
				if (screenPixels[arrayInx] != nextByte) { collision = true; }
			}
		}
		else {
			u8 maskT = (1u << xOffset) - 1u;
			u8 maskB = ~maskT;
			if /*constexpr*/ (x > 7 * 8)
				for (int i = 0; i < nBytes; ++i) { // If the sprite is slightly off the screen on the x axis
					u8 nextByte = RAM.RB(addr + i);
					//unsigned yAdj = (y + i) % 32;
					unsigned yAdj = (y * 8 + i * 8) % 256;
					unsigned arrayInx = 7 + yAdj;
					screenPixels[arrayInx] = ((nextByte >> xOffset) & maskT) ^ screenPixels[arrayInx];
					screenPixels[yAdj] = ((nextByte << (8 - xOffset) & maskB) ^ screenPixels[yAdj]);
					if (screenPixels[arrayInx] != maskT) { collision = true; }
					else if (screenPixels[yAdj] != maskB) { collision = true; }
				}
			else
				for (int i = 0; i < nBytes; ++i) {
					u8 nextByte = RAM.RB(addr + i);
					//unsigned yAdj = (y + i) % 32
					unsigned yAdj = (y * 8 + i * 8) % 256;
					unsigned arrInxHi = (x / 8) + yAdj;
					unsigned arrInxLo = arrInxHi + 1;
					screenPixels[arrInxHi] = ((nextByte >> xOffset) & maskT) ^ screenPixels[arrInxHi];
					screenPixels[arrInxLo] = ((nextByte << (8 - xOffset) & maskB) ^ screenPixels[arrInxLo]);
					if (screenPixels[arrInxHi] != maskT) { collision = true; }
					else if (screenPixels[arrInxLo] != maskB) { collision = true; }
				}
		}
		return collision;
	}

	void DrawScaledPix(SDL_Surface* surf, const int &x, const int &y) {
		for (int i = 0; i < SCALE; ++i) {
			for (int j = 0; j < SCALE; j++) {
				sp::DrawPixel(surf, (x * SCALE) + i, (y * SCALE) + j);
			}
		}
	}

	void draw() {
		SDL_FillRect(surfBuffer.get(), NULL, 0); // Clear Buffer Screen
		for (int i = 0; i < 256; ++i) // Check all bytes in screenPixels array
			for (int j = 7; j >= 0; --j) { // Check bits of the byte
				u8 bitMask = 1u << j;
				bool pixBit = screenPixels[i] & bitMask;
				if(pixBit) {
					DrawScaledPix(surfBuffer.get(), (i % 8) * 8 + (7 - j), i / 8);
					//sp::DrawPixel(surfBuffer.get(), (i % 64) + j, i / 64);
				}
			}
		SDL_BlitSurface(surfBuffer.get(), 0, winSurface.get(), 0);
		SDL_UpdateWindowSurface(win.get());
	}
};

struct Chip8 { // Chip 8 Processor: Originally an interpreter for the TELMAC
	std::array<u8, 16> regs{}; // General Registers from v0 - vf
	std::array<bool, 16> io{ { false } };
	// vf is used for a special flag
	u8 dt{ 0 }, st{ 0 };	// Delay and Sound Timers
	u16 i{ 0 }; // Memory Index
	u16 pc = 0x200; // Program Counter
					//u8 sp; no need due to vector methods // Stack Pointer
	Stack stack; // Stack which contains return addresses
	Uint32 tickStart; // Begining of a cycle
	Uint32 tickTimer; // Syncs timers to 50 hz
	Display disp;
	Memory<u8> RAM;
	unsigned clockCycle = 5000; // Hz
	Uint32 cycleMax = 1000 / clockCycle;
	bool running = true;

	Chip8() {
		tickStart = SDL_GetTicks();
		tickTimer = tickStart;
		loadFont();
	}

	void tick() { // The clock cycle of the chip8
		Uint32 currentTick = SDL_GetTicks();
		Uint32 tickTime = tickStart - currentTick;
		updateTimers();
		if (tickTime < cycleMax)
			SDL_Delay(cycleMax - tickTime);
		tickStart = SDL_GetTicks();
	}

	void updateTimers() { // Decrements Timers at a rate of 50 hz if greater than zero
		Uint32 currentTick = SDL_GetTicks();
		Uint32 tickTime = currentTick - tickTimer;
		const Uint32 timerRegRate = 1000 / 50;
		if (tickTime > timerRegRate) {
			tickTimer = SDL_GetTicks();
			if (dt > 0) --dt;
			if (st > 0) --st;
		}
	}

	void loadFont() { // Loads built in native font into memory
		u8 fontp = 0x0000;
		for (unsigned n : {0xf999f, 0x26227, 0xf1f8f, 0xf1f1f,
			0x99f11, 0xf8f1f, 0xf8f9f, 0xf1244,
			0xf9f9f, 0xf9f1f, 0xf9f99, 0xe9e9e,
			0xf888f, 0xe999e, 0xf8f8f, 0xf8f88})
		{
			for (int i = 4; i > 0; --i) {
				RAM.WB(fontp++, (n >> ((i - 1) * 4)) & 0xf0);
			}
			RAM.WB(fontp++, (n << 4) & 0xf0);
		}
	}

	bool keyIsPressed(const u8 & key) {
		if (io[key]) { return true; }
		else { return false; }
	}

	void checkInput() {
		SDL_Event e;
		while (SDL_PollEvent(&e)) {
			if (e.type == SDL_QUIT)
				running = false;
			if (e.type == SDL_KEYDOWN)
				switch (e.key.keysym.sym) {
				case SDLK_ESCAPE:
					running = false;
					break;
				case SDLK_x:
					io[0] = true;
					break;
				case SDLK_1:
					io[1] = true;
					break;
				case SDLK_2:
					io[2] = true;
					break;
				case SDLK_3:
					io[3] = true;
					break;
				case SDLK_q:
					io[4] = true;
					break;
				case SDLK_w:
					io[5] = true;
					break;
				case SDLK_e:
					io[6] = true;
					break;
				case SDLK_a:
					io[7] = true;
					break;
				case SDLK_s:
					io[8] = true;
					break;
				case SDLK_d:
					io[9] = true;
					break;
				case SDLK_z:
					io[0xa] = true;
					break;
				case SDLK_c:
					io[0xb] = true;
					break;
				case SDLK_4:
					io[0xc] = true;
					break;
				case SDLK_r:
					io[0xd] = true;
					break;
				case SDLK_f:
					io[0xe] = true;
					break;
				case SDLK_v:
					io[0xf] = true;
					break;
				default:
					for (int i = 0; i < 16; ++i) {
						io[i] = false;
					}
					break;
				}
		}

	}

	u8 getPressedKey() { // Returns first pressed key when one is pressed.
		printf("getPressedKey()\n");
		u8 key = 0;
		while (!io[key]) {
			if (key >= 15) {
				key = 0;
				checkInput();
			}
			key++;
		}
		printf("%d\n", key);
		return key;
	}



	void exe(const u16 & opcode) {
		u8 n0, n1, n2, n3;
		n0 = (0xF000 & opcode) >> 12; // opcode broken up into nibbles 
		n1 = (0x0F00 & opcode) >> 8;
		n2 = (0x00F0 & opcode) >> 4;
		n3 = (0x000F & opcode);
		switch (n0) {
		case 0x0:
			switch (n3) {
			case 0x0: // CLS
				disp.clear();
				disp.draw();
				break;
			case 0xe: // RET
				pc = stack.back();
				stack.pop_back();	
				break;
			}
			break;
		case 0x1: // JP addr
			pc = (opcode & 0x0fff);
			pc -= 2; // counters the inc from main op
			break;
		case 0x2: // CALL addr
			stack.push_back(pc);
			pc = (opcode & 0x0fff);
			pc -= 2; // counters the inc from main op
			break;
		case 0x3: // SE Vx, byte
			if (regs[n1] == (opcode & 0x00ff))
				pc += 2;
			break;
		case 0x4: // SNE Vx, byte
			if (regs[n1] != (opcode & 0x00ff))
				pc += 2;
			break;
		case 0x5: // SE Vx, Vy
			if (regs[n1] == regs[n2])
				pc += 2;
			break;
		case 0x6: // LD Vx, byte
			regs[n1] = (opcode & 0x00ff);
			break;
		case 0x7: // ADD Vx, byte
			regs[n1] += (opcode & 0x00ff);
			break;
		case 0x8:
			switch (n3) {
			case 0x0: // LD Vx, Vy
				regs[n1] = regs[n2];
				break;
			case 0x1: // OR Vx, Vy
				regs[n1] = regs[n1] | regs[n2];
				break;
			case 0x2: // AND Vx, Vy
				regs[n1] = regs[n1] & regs[n2];
				break;
			case 0x3: // XOR Vx, Vy
				regs[n1] = regs[n1] ^ regs[n2];
				break;
			case 0x4: // ADD Vx, Vy
				regs[n1] += regs[n2];
				(regs[n1] < regs[n2]) ? regs[0xf] = 1 : regs[0xf] = 0;
				break;
			case 0x5: // SUB Vx, Vy
				(regs[n1] > regs[n2]) ? regs[0xf] = 1 : regs[0xf] = 0;
				regs[n1] -= regs[n2];
				break;
			case 0x6: // SHR Vx {, Vy}
				(regs[n1] & 0x1) ? regs[0xf] = 1 : regs[0xf] = 0;
				regs[n1] = regs[n1] >> 1;
				break;
			case 0x7: // SUBN Vx, Vy
				(regs[n2] > regs[n1]) ? regs[0xf] = 1 : regs[0xf] = 0;
				regs[n1] = regs[n2] - regs[n1];
				break;
			case 0xe: // SHL Vx {, Vy}
				(regs[n1] & 0x80) ? regs[0xf] = 1 : regs[0xf] = 0;
				regs[n1] = regs[n1] << 1;
				break;
			}
			break;
		case 0x9: // SNE Vx, Vy
			if (regs[n1] != regs[n2])
				pc += 2;
			break;
		case 0xa: // LD I, addr
			i = (opcode & 0x0fff);
			break;
		case 0xb: // JP V0, addr
			pc = regs[0x0] + (opcode & 0x0fff);
			pc -= 2; // counters the inc from main op
			break;
		case 0xc: // RND Vx, byte
			regs[n1] = (rand() % 256) & (opcode & 0x00ff);
			break;
		case 0xd: // DRW Vx, Vy, nibble
			if (disp.predrawSurf(i, RAM, n3, regs[n1], regs[n2]))
				regs[0xf] = 1;
			else
				regs[0xf] = 0;
			disp.draw();
			break;
		case 0xe:
			switch (n2) {
			case 0x9:
				if (keyIsPressed(regs[n1]))
					pc += 2;
				break;
			case 0xa:
				if (!keyIsPressed(regs[n1]))
					pc += 2;
				break;
			}
			break;
		case 0xf:
			switch ((n2 << 4) | n3) {
			case 0x07: // LD Vx, DT
				regs[n1] = dt;
				break;
			case 0x0a: // LD Vx, K
				regs[n1] = getPressedKey();
				break;
			case 0x15: // LD DT, Vx
				dt = regs[n1];
				break;
			case 0x18: // LD ST, Vx
				st = regs[n1];
				break;
			case 0x1e: // AND I, Vx
				i = i + regs[n1];
				break;
			case 0x29: // LD F, Vx
				i = regs[n1] * 5;
				break;
			case 0x33: // LD B, Vx
				RAM.WB(i, (regs[n1] / 100) % 10);
				RAM.WB(i + 1, (regs[n1] / 10) % 10);
				RAM.WB(i + 2, regs[n1] % 10);
				break;
			case 0x55: // LD [I], Vx
				for (int j = 0; j < n1; ++j) {
					RAM.WB(i + j, regs[j]);
				}
				break;
			case 0x65: // LD Vx, [I]
				for (int j = 0; j < n1; ++j) {
					regs[j] = RAM.RB(i + j);
				}
			}
		}
	}

	void op() {
		u16 opcode = (RAM.RB(pc) << 8) | RAM.RB(pc + 1);
		checkInput();
		exe(opcode);
		pc += 2; // Each instruction is 2 bytes long
		tick();
	}
};


int main(int /*argc*/, char ** argv) {
	srand(time(0));
	Chip8 sys;
	FILE* f = fopen(argv[1], "rb");
	if (f == NULL) perror("File could not be opened");
	else {
		signed n = fgetc(f);
		unsigned memAddr = 0x200;
		while (n != EOF) {
			sys.RAM.WB(memAddr++, u8(n & 0xff));
			n = fgetc(f);
		}

		while (sys.running) {
			sys.op();
		}
	}
	SDL_Quit();
	return 0;
}
