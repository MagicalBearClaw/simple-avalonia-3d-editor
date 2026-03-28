#include <SDL3/SDL.h>
#include <Renderer.h>

#include <cstdio>

int main()
{
    constexpr uint32_t kInitWidth  = 1280;
    constexpr uint32_t kInitHeight = 720;

    if (!SDL_Init(SDL_INIT_VIDEO)) {
        SDL_Log("SDL_Init failed: %s", SDL_GetError());
        return 1;
    }

    SDL_Window* window = SDL_CreateWindow("renderer_test", kInitWidth, kInitHeight,
                                          SDL_WINDOW_RESIZABLE);
    if (!window) {
        SDL_Log("SDL_CreateWindow failed: %s", SDL_GetError());
        SDL_Quit();
        return 1;
    }

    SDL_Renderer* sdlRenderer = SDL_CreateRenderer(window, nullptr);
    if (!sdlRenderer) {
        SDL_Log("SDL_CreateRenderer failed: %s", SDL_GetError());
        SDL_DestroyWindow(window);
        SDL_Quit();
        return 1;
    }

    Renderer renderer(kInitWidth, kInitHeight);

    SDL_Texture* texture = SDL_CreateTexture(sdlRenderer,
                                             SDL_PIXELFORMAT_BGRA32,
                                             SDL_TEXTUREACCESS_STREAMING,
                                             static_cast<int>(kInitWidth),
                                             static_cast<int>(kInitHeight));
    if (!texture) {
        SDL_Log("SDL_CreateTexture failed: %s", SDL_GetError());
        SDL_DestroyRenderer(sdlRenderer);
        SDL_DestroyWindow(window);
        SDL_Quit();
        return 1;
    }

    uint32_t currentWidth  = kInitWidth;
    uint32_t currentHeight = kInitHeight;
    bool     running       = true;

    while (running) {
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            if (event.type == SDL_EVENT_QUIT) {
                running = false;
            } else if (event.type == SDL_EVENT_WINDOW_RESIZED) {
                currentWidth  = static_cast<uint32_t>(event.window.data1);
                currentHeight = static_cast<uint32_t>(event.window.data2);

                SDL_DestroyTexture(texture);
                renderer.Resize(currentWidth, currentHeight);

                texture = SDL_CreateTexture(sdlRenderer,
                                            SDL_PIXELFORMAT_BGRA32,
                                            SDL_TEXTUREACCESS_STREAMING,
                                            static_cast<int>(currentWidth),
                                            static_cast<int>(currentHeight));
                if (!texture) {
                    SDL_Log("SDL_CreateTexture failed after resize: %s", SDL_GetError());
                    running = false;
                }
            }
        }

        if (!running) break;

        renderer.RenderFrame();

        const int pitch = static_cast<int>(currentWidth) * 4;
        SDL_UpdateTexture(texture, nullptr, renderer.GetPixelData(), pitch);

        SDL_RenderClear(sdlRenderer);
        SDL_RenderTexture(sdlRenderer, texture, nullptr, nullptr);
        SDL_RenderPresent(sdlRenderer);
    }

    SDL_DestroyTexture(texture);
    SDL_DestroyRenderer(sdlRenderer);
    SDL_DestroyWindow(window);
    SDL_Quit();

    return 0;
}
