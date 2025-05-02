#include "inc/SDL.h"
#include "inc/SDL_ttf.h"
#include "inc/tinyxml2.h"
#include <bits/stdc++.h>

using namespace std;
using namespace tinyxml2;

const int WIDTH = 1280;
const int HEIGHT = 739;

SDL_Window* window = nullptr;
SDL_Renderer* renderer = nullptr;
TTF_Font* font = nullptr;

// Cấu trúc Node và Way
struct Node {
    double lat, lon;
};

struct Way {
    std::vector<long long> nodeIds;
    std::string name;
};

std::unordered_map<long long, Node> nodeMap;
std::vector<Way> ways;

// Biến toàn cục cho min/max toạ độ
double minLat = 90.0, maxLat = -90.0;
double minLon = 180.0, maxLon = -180.0;

// Zoom/pan control
double scaleFactor = 1.0;
int offsetX = 0, offsetY = 0;
bool isDragging = false;
int mouseStartX = 0, mouseStartY = 0;

// Hàm đọc file OSM
void readOSM(const std::string& filename) {
    XMLDocument doc;
    doc.LoadFile(filename.c_str());

    if (doc.ErrorID()) {
        std::cerr << "Error loading OSM file\n";
        return;
    }

    XMLElement* osm = doc.RootElement();

    // Đọc node
    for (XMLElement* nodeElement = osm->FirstChildElement("node");
         nodeElement;
         nodeElement = nodeElement->NextSiblingElement("node")) {

        long long id = nodeElement->Int64Attribute("id");
        double lat = nodeElement->DoubleAttribute("lat");
        double lon = nodeElement->DoubleAttribute("lon");

        Node node = {lat, lon};
        nodeMap[id] = node;

        minLat = std::min(minLat, lat);
        maxLat = std::max(maxLat, lat);
        minLon = std::min(minLon, lon);
        maxLon = std::max(maxLon, lon);
    }

    // Đọc way
    for (XMLElement* wayElement = osm->FirstChildElement("way");
         wayElement;
         wayElement = wayElement->NextSiblingElement("way")) {

        Way way;
        for (XMLElement* nd = wayElement->FirstChildElement("nd"); nd; nd = nd->NextSiblingElement("nd")) {
            long long ref = nd->Int64Attribute("ref");
            if (nodeMap.count(ref)) {
                way.nodeIds.push_back(ref);
            }
        }

        for (XMLElement* tag = wayElement->FirstChildElement("tag"); tag; tag = tag->NextSiblingElement("tag")) {
            const char* key = tag->Attribute("k");
            const char* value = tag->Attribute("v");
            if (key && value && std::string(key) == "name") {
                way.name = value;
            }
        }

        if (way.nodeIds.size() >= 2)
            ways.push_back(way);
    }
}

// Chuyển đổi toạ độ
int latToY(double lat) {
    return static_cast<int>((maxLat - lat) / (maxLat - minLat) * HEIGHT * scaleFactor) + offsetY;
}

int lonToX(double lon) {
    return static_cast<int>((lon - minLon) / (maxLon - minLon) * WIDTH * scaleFactor) + offsetX;
}

// Vẽ bản đồ
void drawMap() {
    SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255); // Đen

    for (const auto& way : ways) {
        for (size_t i = 0; i < way.nodeIds.size() - 1; ++i) {
            const Node& n1 = nodeMap[way.nodeIds[i]];
            const Node& n2 = nodeMap[way.nodeIds[i + 1]];

            int x1 = lonToX(n1.lon);
            int y1 = latToY(n1.lat);
            int x2 = lonToX(n2.lon);
            int y2 = latToY(n2.lat);

            SDL_RenderDrawLine(renderer, x1, y1, x2, y2);
        }
    }
}

int main(int argc, char* argv[]) {
    SDL_Init(SDL_INIT_VIDEO);
    TTF_Init();

    window = SDL_CreateWindow("OSM Map Viewer", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, WIDTH, HEIGHT, 0);
    renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED);
    font = TTF_OpenFont("arial.ttf", 16);

    readOSM("map.osm");

    bool quit = false;
    SDL_Event e;

    while (!quit) {
        while (SDL_PollEvent(&e)) {
            if (e.type == SDL_QUIT) {
                quit = true;
            }

            // Mouse drag
            else if (e.type == SDL_MOUSEBUTTONDOWN && e.button.button == SDL_BUTTON_LEFT) {
                isDragging = true;
                mouseStartX = e.button.x;
                mouseStartY = e.button.y;
            }
            else if (e.type == SDL_MOUSEBUTTONUP && e.button.button == SDL_BUTTON_LEFT) {
                isDragging = false;
            }
            else if (e.type == SDL_MOUSEMOTION && isDragging) {
                int dx = e.motion.x - mouseStartX;
                int dy = e.motion.y - mouseStartY;
                offsetX += dx;
                offsetY += dy;
                mouseStartX = e.motion.x;
                mouseStartY = e.motion.y;
            }

            // Mouse wheel for zoom
            else if (e.type == SDL_MOUSEWHEEL) {
                int centerX = WIDTH / 2;
                int centerY = HEIGHT / 2;

                double beforeZoomX = (centerX - offsetX) / scaleFactor;
                double beforeZoomY = (centerY - offsetY) / scaleFactor;

                if (e.wheel.y > 0)
                    scaleFactor *= 1.1;
                else if (e.wheel.y < 0)
                    scaleFactor /= 1.1;

                double afterZoomX = (centerX - offsetX) / scaleFactor;
                double afterZoomY = (centerY - offsetY) / scaleFactor;

                offsetX += static_cast<int>((afterZoomX - beforeZoomX) * scaleFactor);
                offsetY += static_cast<int>((afterZoomY - beforeZoomY) * scaleFactor);
            }
        }

        SDL_SetRenderDrawColor(renderer, 255, 255, 255, 255);
        SDL_RenderClear(renderer);

        drawMap();

        SDL_RenderPresent(renderer);
    }

    TTF_CloseFont(font);
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    TTF_Quit();
    SDL_Quit();
    return 0;
}
