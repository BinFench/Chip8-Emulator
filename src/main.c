#include <stdio.h>
#include <stdbool.h>
#include <stdlib.h>
#include <SDL2/SDL.h>
#include <SDL2/SDL_image.h>
#include <SDL2/SDL_timer.h>

//define chip 8 system variables
uint16_t opcode;
uint8_t *memory;
//15 general purpose registers, 16th is carry flag
uint8_t *V;
//Index register and Program counter (0x000 - 0xFFF)
uint16_t I;
uint16_t pc;
//2048 pixel screen
uint32_t *gfx;
//2 60Hz timers
uint8_t delay_timer;
uint8_t sound_timer;
//Stack and stack pointer
uint16_t *stack;
uint8_t sp;
//HEX based keypad (0x0 - 0xF)
uint8_t *key;
uint8_t currentKey;
//Draw screen flag
bool drawFlag;

/* chip 8 memory map:
0x000-0x1FF - Chip 8 interpreter (contains font set in emu)
0x050-0x0A0 - Used for the built in 4x5 pixel font set (0-F)
0x200-0xFFF - Program ROM and work RAM
*/

//Setup graphics
SDL_Window *window = NULL;
SDL_Surface *screenSurface = NULL;
const int SCREEN_WIDTH = 640;
const int SCREEN_HEIGHT = 320;
bool quit = false;
uint8_t fontset[80] =
    {
        0xF0, 0x90, 0x90, 0x90, 0xF0, // 0
        0x20, 0x60, 0x20, 0x20, 0x70, // 1
        0xF0, 0x10, 0xF0, 0x80, 0xF0, // 2
        0xF0, 0x10, 0xF0, 0x10, 0xF0, // 3
        0x90, 0x90, 0xF0, 0x10, 0x10, // 4
        0xF0, 0x80, 0xF0, 0x10, 0xF0, // 5
        0xF0, 0x80, 0xF0, 0x90, 0xF0, // 6
        0xF0, 0x10, 0x20, 0x40, 0x40, // 7
        0xF0, 0x90, 0xF0, 0x90, 0xF0, // 8
        0xF0, 0x90, 0xF0, 0x10, 0xF0, // 9
        0xF0, 0x90, 0xF0, 0x90, 0x90, // A
        0xE0, 0x90, 0xE0, 0x90, 0xE0, // B
        0xF0, 0x80, 0x80, 0x80, 0xF0, // C
        0xE0, 0x90, 0x90, 0x90, 0xE0, // D
        0xF0, 0x80, 0xF0, 0x80, 0xF0, // E
        0xF0, 0x80, 0xF0, 0x80, 0x80  // F
};

void init()
{
    memory = calloc(sizeof(uint8_t), 4096);
    for (int i = 0; i < 80; ++i)
    {
        memory[0x50 + i] = fontset[i];
    }

    V = calloc(sizeof(uint8_t), 16);
    gfx = calloc(sizeof(uint32_t), 64 * 32);
    stack = calloc(sizeof(uint16_t), 16);
    key = calloc(sizeof(uint8_t), 16);
    delay_timer = 0;
    sound_timer = 0;
    sp = 0;
    pc = 0x200;
    I = 0;
    return;
}

Uint32 getpixel(SDL_Surface *surface, int x, int y)
{
    int bpp = surface->format->BytesPerPixel;
    /* Here p is the address to the pixel we want to retrieve */
    Uint8 *p = (Uint8 *)surface->pixels + y * surface->pitch + x * bpp;
    switch (bpp)
    {
    case 1:
        return *p;
        break;

    case 2:
        return *(Uint16 *)p;
        break;

    case 3:
        if (SDL_BYTEORDER == SDL_BIG_ENDIAN)
            return p[0] << 16 | p[1] << 8 | p[2];
        else
            return p[0] | p[1] << 8 | p[2] << 16;
        break;

    case 4:
        return *(Uint32 *)p;
        break;

    default:
        return 0; /* shouldn't happen, but avoids warnings */
    }
}

void loadROM(FILE *fp)
{
    fseek(fp, 0L, SEEK_END);
    int size = ftell(fp);
    fseek(fp, 0l, SEEK_SET);
    fread(memory + 0x200, sizeof(uint8_t), size, fp);
    return;
}

void emulateCycle()
{
    time_t t;
    srand((unsigned)time(&t));

    //Fetch Opcode
    opcode = (memory[pc] << 8 | memory[pc + 1]);
    unsigned char vX = (opcode & 0x0F00u) >> 8u;
    unsigned char vY = (opcode & 0x00F0u) >> 4u;
    pc += 2;

    //Decode and then execute opcode
    switch (opcode & 0xF000)
    {
    case 0x0000:
        switch (opcode & 0x00EE)
        {
        case 0x00E0:
            //Clear display
            memset(gfx, 0, sizeof(gfx));
            break;

        case 0x00EE:
            //Return from subroutine
            pc = stack[sp];
            sp--;
            break;
        }
        break;

    case 0x1000:
        //Jump to nnn
        pc = opcode & 0x0FFFu;
        break;

    case 0x2000:
        //Call subroutine at nnn
        sp++;
        stack[sp] = pc;
        pc = opcode & 0x0FFFu;
        break;

    case 0x3000:
    {
        //Skip instruction if vX = kk
        uint8_t byte = opcode & 0x00FFu;
        if (V[vX] == byte)
        {
            pc += 2;
        }
        break;
    }

    case 0x4000:
    {
        //Skip instruction if vX != kk
        uint8_t byte = opcode & 0x00FFu;
        if (V[vX] != byte)
        {
            pc += 2;
        }
        break;
    }

    case 0x5000:
        //Skip instruction if vX = vY
        if (V[vX] == V[vY])
        {
            pc += 2;
        }
        break;

    case 0x6000:
        //vX = kk
        V[vX] = opcode & 0x00FFu;
        break;

    case 0x7000:
    {
        //vX += kk
        uint8_t byte = opcode & 0x00FFu;
        V[vX] += byte;
        break;
    }

    case 0x8000:
        switch (opcode & 0x000F)
        {
        case 0x0000:
            //vX = vY
            V[vX] = V[vY];
            break;

        case 0x0001:
            //vX |= vY
            V[vX] |= V[vY];
            break;

        case 0x0002:
            //vX &= vY
            V[vX] &= V[vY];
            break;

        case 0x0003:
            //vX ^= vY
            V[vX] ^= V[vY];
            break;

        case 0x0004:
        {
            //vX += vY with carry
            uint16_t sum = V[vX] + V[vY];
            if (sum > 255)
            {
                V[0xF] = 1;
            }
            else
            {
                V[0xF] = 0;
            }
            V[vX] = sum & 0xFFu;

            break;
        }

        case 0x0005:
            //vX -= vY, vF = not borrow
            V[0xF] = 0;
            if (V[vX] > V[vY])
            {
                V[0xF] = 1;
            }
            V[vX] -= V[vY];
            break;

        case 0x0006:
            //Set vX = vX SHR 1
            V[0xF] = V[vX] & 0x1u;
            V[vX] >>= 1;
            break;

        case 0x0007:
            //VF = vY > vX; vX = vY - vX;
            V[0xF] = 0;
            if (V[vY] > V[vX])
            {
                V[0xF] = 1;
            }

            V[vX] = V[vY] - V[vX];
            break;

        case 0x000E:
            //vX = vX SHL 1
            V[0xF] = (V[vX] & 0x80u) >> 7u;
            V[vX] <<= 1;
            break;
        }
        break;

    case 0x9000:
        //Skip next instruction if vX != vY
        if (V[vX] != V[vY])
        {
            pc += 2;
        }
        break;

    case 0xA000:
        //set I = nnn
        I = opcode & 0x0FFFu;
        break;

    case 0xB000:
        //Jump to nnn + v0
        pc = (opcode & 0x0FFFu) + V[0];
        break;

    case 0xC000:
        //set vX = random byte AND kk
        V[vX] = (rand() % 256) & (opcode & 0x00FFu);
        break;

    case 0xD000:
    {
        //DRW vX, vY, nibble
        uint8_t height = opcode & 0x000Fu;
        uint8_t xPos = V[vX] % 64;
        uint8_t yPos = V[vY] % 32;
        V[0xF] = 0;

        for (int i = 0; i < height; ++i)
        {
            uint8_t sprite = memory[I + i];
            for (int j = 0; j < 8; ++j)
            {
                uint8_t spritePixel = sprite & (0x80u >> j);
                uint32_t *screenPixel = &gfx[(yPos + i) * 64 + (xPos + j)];
                if (spritePixel)
                {
                    if (*screenPixel == 0xFFFFFFFF)
                    {
                        V[0xF] = 1;
                    }
                    *screenPixel ^= 0xFFFFFFFF;
                }
            }
        }
        break;
    }

    case 0xE000:
        switch (opcode & 0x00FF)
        {
        case 0x009E:
            //Skip next instruction if key[vX] is true
            if (key[V[vX]])
            {
                pc += 2;
            }
            break;

        case 0x00A1:
            //Skip next instruction if key[vX] is false
            if (!key[V[vX]])
            {
                pc += 2;
            }
            break;
        }
        break;

    case 0xF000:
        switch (opcode & 0x00FF)
        {
        case 0x0007:
            //Set vX = delay timer value
            V[vX] = delay_timer;
            break;

        case 0x000A:
            //Wait for key press, store value of key in vX
            if (!currentKey)
            {
                pc -= 2;
                return;
            }
            V[vX] = currentKey - 1;
            break;

        case 0x0015:
            //Set delay timer = vX
            delay_timer = V[vX];
            break;

        case 0x0018:
            //Set sound timer = vX
            sound_timer = V[vX];
            break;

        case 0x001E:
            //Set I += vX
            I += V[vX];
            break;

        case 0x0029:
            //Set I = location of sprite for digit vX
            I = 0x50 + (V[vX] * 5);
            break;

        case 0x0033:
        {
            //Store BCD representation of vX in I, I+1, I+2
            int number = V[vX];
            // Ones-place
            memory[I + 2] = number % 10;
            number /= 10;

            // Tens-place
            memory[I + 1] = number % 10;
            number /= 10;

            // Hundreds-place
            memory[I] = number % 10;
            break;
        }

        case 0x0055:
            //Store v0-x in memory starting at I
            for (int i = 0; i <= vX; ++i)
            {
                memory[I + i] = V[i];
            }
            break;

        case 0x0065:
            //Read v0-x in memory starting at I
            for (int i = 0; i <= vX; ++i)
            {
                V[i] = memory[I + i];
            }
            break;
        }
        break;
    }

    //Update Timers
    if (delay_timer > 0)
    {
        delay_timer--;
    }

    if (sound_timer > 0)
    {
        sound_timer--;
    }

    return;
}

void updateScreen()
{
    for (int i = 0; i < 64; i++)
    {
        for (int j = 0; j < 32; j++)
        {
            SDL_Rect rect;
            rect.x = i * 10;
            rect.y = j * 10;
            rect.w = 10;
            rect.h = 10;
            if ((gfx[64 * j + i] > 0))
            {
                SDL_FillRect(screenSurface, &rect, SDL_MapRGB(screenSurface->format, 255, 255, 255));
            }
            else
            {
                SDL_FillRect(screenSurface, &rect, SDL_MapRGB(screenSurface->format, 0, 0, 0));
            }
        }
    }
    SDL_UpdateWindowSurface(window);
}

void setKeys()
{
    SDL_Event e;
    while (SDL_PollEvent(&e) != 0)
    {
        switch (e.type)
        {
        case SDL_QUIT:
            quit = true;
            break;

        case SDL_KEYDOWN:
            if (e.key.keysym.sym == SDLK_1)
            {
                key[0] = 1;
                currentKey = 1;
            }
            if (e.key.keysym.sym == SDLK_2)
            {
                key[1] = 1;
                currentKey = 2;
            }
            if (e.key.keysym.sym == SDLK_3)
            {
                key[2] = 1;
                currentKey = 3;
            }
            if (e.key.keysym.sym == SDLK_4)
            {
                key[3] = 1;
                currentKey = 4;
            }
            if (e.key.keysym.sym == SDLK_q)
            {
                key[4] = 1;
                currentKey = 5;
            }
            if (e.key.keysym.sym == SDLK_w)
            {
                key[5] = 1;
                currentKey = 6;
            }
            if (e.key.keysym.sym == SDLK_e)
            {
                key[6] = 1;
                currentKey = 7;
            }
            if (e.key.keysym.sym == SDLK_r)
            {
                key[7] = 1;
                currentKey = 8;
            }
            if (e.key.keysym.sym == SDLK_a)
            {
                key[8] = 1;
                currentKey = 9;
            }
            if (e.key.keysym.sym == SDLK_s)
            {
                key[9] = 1;
                currentKey = 10;
            }
            if (e.key.keysym.sym == SDLK_d)
            {
                key[10] = 1;
                currentKey = 11;
            }
            if (e.key.keysym.sym == SDLK_f)
            {
                key[11] = 1;
                currentKey = 12;
            }
            if (e.key.keysym.sym == SDLK_z)
            {
                key[12] = 1;
                currentKey = 13;
            }
            if (e.key.keysym.sym == SDLK_x)
            {
                key[13] = 1;
                currentKey = 14;
            }
            if (e.key.keysym.sym == SDLK_c)
            {
                key[14] = 1;
                currentKey = 15;
            }
            if (e.key.keysym.sym == SDLK_v)
            {
                key[15] = 1;
                currentKey = 16;
            }
            break;

        case SDL_KEYUP:
            if (e.key.keysym.sym == SDLK_1)
            {
                key[0] = 0;
                if (currentKey == 1)
                {
                    currentKey = 0;
                }
            }
            if (e.key.keysym.sym == SDLK_2)
            {
                key[1] = 0;
                if (currentKey == 2)
                {
                    currentKey = 0;
                }
            }
            if (e.key.keysym.sym == SDLK_3)
            {
                key[2] = 0;
                if (currentKey == 3)
                {
                    currentKey = 0;
                }
            }
            if (e.key.keysym.sym == SDLK_4)
            {
                key[3] = 0;
                if (currentKey == 4)
                {
                    currentKey = 0;
                }
            }
            if (e.key.keysym.sym == SDLK_q)
            {
                key[4] = 0;
                if (currentKey == 5)
                {
                    currentKey = 0;
                }
            }
            if (e.key.keysym.sym == SDLK_w)
            {
                key[5] = 0;
                if (currentKey == 6)
                {
                    currentKey = 0;
                }
            }
            if (e.key.keysym.sym == SDLK_e)
            {
                key[6] = 0;
                if (currentKey == 7)
                {
                    currentKey = 0;
                }
            }
            if (e.key.keysym.sym == SDLK_r)
            {
                key[7] = 0;
                if (currentKey == 8)
                {
                    currentKey = 0;
                }
            }
            if (e.key.keysym.sym == SDLK_a)
            {
                key[8] = 0;
                if (currentKey == 9)
                {
                    currentKey = 0;
                }
            }
            if (e.key.keysym.sym == SDLK_s)
            {
                key[9] = 0;
                if (currentKey == 10)
                {
                    currentKey = 0;
                }
            }
            if (e.key.keysym.sym == SDLK_d)
            {
                key[10] = 0;
                if (currentKey == 11)
                {
                    currentKey = 0;
                }
            }
            if (e.key.keysym.sym == SDLK_f)
            {
                key[11] = 0;
                if (currentKey == 12)
                {
                    currentKey = 0;
                }
            }
            if (e.key.keysym.sym == SDLK_z)
            {
                key[12] = 0;
                if (currentKey == 13)
                {
                    currentKey = 0;
                }
            }
            if (e.key.keysym.sym == SDLK_x)
            {
                key[13] = 0;
                if (currentKey == 14)
                {
                    currentKey = 0;
                }
            }
            if (e.key.keysym.sym == SDLK_c)
            {
                key[14] = 0;
                if (currentKey == 15)
                {
                    currentKey = 0;
                }
            }
            if (e.key.keysym.sym == SDLK_v)
            {
                key[15] = 0;
                if (currentKey == 16)
                {
                    currentKey = 0;
                }
            }
            break;

        default:
            break;
        }
    }
}

int main(int argc, char *args[])
{
    if (argc != 2)
    {
        printf("Emulator takes one argument: ROM name.\n");
        return 0;
    }

    init();

    FILE *fp;
    char *msg;
    asprintf(&msg, "../roms/%s", args[1]);
    printf("%s\n", msg);
    fp = fopen(msg, "r");
    loadROM(fp);
    fclose(fp);
    free(msg);

    if (SDL_Init(SDL_INIT_VIDEO) < 0)
    {
        printf("SDL could not initialize! SDL_Error: %s\n", SDL_GetError());
    }
    else
    {
        window = SDL_CreateWindow("SDL Tutorial", SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED, SCREEN_WIDTH, SCREEN_HEIGHT, SDL_WINDOW_SHOWN);

        if (window == NULL)
        {
            printf("Window could not be created! SDL_Error: %s\n", SDL_GetError());
        }
        else
        {
            screenSurface = SDL_GetWindowSurface(window);
            SDL_FillRect(screenSurface, NULL, SDL_MapRGB(screenSurface->format, 0, 0, 0));
            SDL_UpdateWindowSurface(window);
            while (!quit)
            {
                emulateCycle();
                updateScreen();
                setKeys();
                SDL_Delay(17);
            }
        }
    }
    SDL_DestroyWindow(window);
    SDL_Quit();

    free(memory);
    free(stack);
    free(gfx);
    free(key);
    free(V);

    return 0;
}
