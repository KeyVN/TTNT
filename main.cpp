#include "SDL.h"
#include "SDL_ttf.h"
#include "SDL2_gfxPrimitives.h"
#include "SDL_image.h"
#include <bits/stdc++.h>
#include "pugixml.hpp"

using namespace std;

struct OsmNode {
    long long id;
    double lat, lon; // Tọa độ gốc từ OSM
    int pixelX = -1, pixelY = -1; // Tọa độ pixel trên map (sau khi chuyển đổi)
};

struct StreetWay {
    string name;
    vector<long long> nodeIds; // Danh sách ID của các node tạo nên đường này
};

// Biến toàn cục (hoặc trong một class quản lý map)
unordered_map<long long, OsmNode> osmNodes;
vector<StreetWay> streetWays;
TTF_Font* streetFont = nullptr; // Có thể dùng font riêng cho tên đường


const int WIDTH = 1280;
const int HEIGHT = 697;

SDL_Window* window = nullptr; // cửa sổ chính
SDL_Renderer* renderer = nullptr;
TTF_Font* font = nullptr; // font chữ thôi, không có gì cả

SDL_Surface* mapSurface = nullptr; // load ảnh vào đây
SDL_Texture* mapTexture = nullptr; // vẽ lên renderer

float zoomFactor = 1.0f;  // tỉ lệ zoom
float minZoomFactor = 1.0f;
int offsetX = 0, offsetY = 0;
bool isDragging = false;  // check dragging
int dragStartX = 0, dragStartY = 0; 

string inputTextA, inputTextB;
bool isStart = true;
SDL_Point pointA = {-1, -1}, pointB = {-1, -1};

const double MAP_MIN_LAT = 21.035; //Vĩ độ thấp nhất của map
const double MAP_MAX_LAT = 21.045; //Vĩ độ cao nhất của map
const double MAP_MIN_LON = 105.820; //Kinh độ thấp nhất của map
const double MAP_MAX_LON = 105.840; //Kinh độ cao nhất của map

void loadMap(const char* fileMap) {
    mapSurface = IMG_Load(fileMap);  // load ảnh vào surface

    // Tạo texture từ surface
    mapTexture = SDL_CreateTextureFromSurface(renderer, mapSurface);
    // zoom mượt hơn ? idk
    SDL_SetTextureScaleMode(mapTexture, SDL_ScaleModeLinear);

    SDL_FreeSurface(mapSurface);
}

void drawTextboxWithShadow(int x, int y, int w, int h, int r, SDL_Color border, SDL_Color fill) {
    roundedBoxRGBA(renderer, x + 2, y + 2, x + w + 2, y + h + 2, r, 0, 0, 0, 80); // đổ bóng 
    roundedBoxRGBA(renderer, x, y, x + w, y + h, r, fill.r, fill.g, fill.b, fill.a); // nền box
    roundedRectangleRGBA(renderer, x, y, x + w, y + h, r, border.r, border.g, border.b, border.a); // viền box
}

void renderText(const string& text, int x, int y) {
    SDL_Color c = {255, 255, 255, 255};
    SDL_Surface* s = TTF_RenderUTF8_Blended(font, text.c_str(), c); // tạo surface từ string
    SDL_Texture* t = SDL_CreateTextureFromSurface(renderer, s); // tạo texture 
    SDL_Rect dst = {x, y, s->w, s->h};

    SDL_FreeSurface(s);
    SDL_RenderCopy(renderer, t, nullptr, &dst);
    SDL_DestroyTexture(t);
}

void drawMarker(SDL_Point p, SDL_Color color) {
    int x = (int)(p.x * zoomFactor) + offsetX;
    int y = (int)(p.y * zoomFactor) + offsetY;
    int r = 6; // radius
    //filledCircleRGBA(renderer, sx + 2, sy + 2, r, 0, 0, 0, 100);
    filledCircleRGBA(renderer, x, y, r, color.r, color.g, color.b, color.a);
    circleRGBA(renderer, x, y, r, 255, 255, 255, 255);
}

void handleZoom(int wheelDirection, int mouseX, int mouseY) {
    float prevZoomFactor = zoomFactor;
    float scaleMultiplier = (wheelDirection > 0) ? 1.1f : 0.9f;
    float newZoomFactor = zoomFactor * scaleMultiplier;

    if (newZoomFactor < minZoomFactor) {
        newZoomFactor = minZoomFactor; // Không cho phép zoom nhỏ hơn mức tối thiểu
    }

    if (abs(newZoomFactor - prevZoomFactor) < 1e-6f) {
        return; // Zoom không thay đổi đủ lớn, không cần làm gì thêm
    }
    zoomFactor = newZoomFactor;
    float zoomRatio = zoomFactor / prevZoomFactor;
    offsetX -= (int)((mouseX - offsetX) * (zoomFactor / prevZoomFactor - 1.0f));
    offsetY -= (int)((mouseY - offsetY) * (zoomFactor / prevZoomFactor - 1.0f));
}

void handlePan(const SDL_Event& e) {
    if (e.type == SDL_MOUSEMOTION && (e.motion.state & SDL_BUTTON_LMASK)) {
        if (!isDragging) {
            isDragging = true;
            dragStartX = e.motion.x;
            dragStartY = e.motion.y;
        } else {
            offsetX += e.motion.x - dragStartX;
            offsetY += e.motion.y - dragStartY;
            dragStartX = e.motion.x;
            dragStartY = e.motion.y;
        }
    }
    else if (e.type == SDL_MOUSEBUTTONUP && e.button.button == SDL_BUTTON_LEFT) {
        isDragging = false;  // Stop dragging
    }
}

SDL_Point parseInputToPoint(const string& text) {
    SDL_Point p = {-1, -1};
    stringstream ss(text);
    string xStr, yStr;
    if (getline(ss, xStr, ',') && getline(ss, yStr)) {
        try {
            p.x = stoi(xStr);
            p.y = stoi(yStr);
        } catch (...) {
            p = {-1, -1}; // lỗi parse
        }
    }
    return p;
}

// Hàm chuyển đổi Lat/Lon sang tọa độ Pixel trên ảnh map
SDL_Point mapLatLonToPixel(double lat, double lon, int mapImageWidth, int mapImageHeight) {
    // Kiểm tra xem lat/lon có nằm trong phạm vi map không (tùy chọn)
    if (lat < MAP_MIN_LAT || lat > MAP_MAX_LAT || lon < MAP_MIN_LON || lon > MAP_MAX_LON) {
       // return {-1, -1}; // Ngoài phạm vi
    }

    double mapLonWidth = MAP_MAX_LON - MAP_MIN_LON;
    double mapLatHeight = MAP_MAX_LAT - MAP_MIN_LAT;

    // Tránh chia cho 0
    if (mapLonWidth == 0 || mapLatHeight == 0) return {-1,-1};

    int pixelX = static_cast<int>(mapImageWidth * (lon - MAP_MIN_LON) / mapLonWidth);
    // Tọa độ Y thường bị ngược (latitude cao hơn -> pixel Y nhỏ hơn)
    int pixelY = static_cast<int>(mapImageHeight * (MAP_MAX_LAT - lat) / mapLatHeight);

    return {pixelX, pixelY};
}

void renderStreetNames(SDL_Renderer* renderer, TTF_Font* font, int mapWidth, int mapHeight) {
    SDL_Color streetColor = {240, 240, 240, 200}; // Màu chữ cho tên đường (hơi mờ)
    SDL_Color streetOutlineColor = {0, 0, 0, 200}; // Viền chữ (tùy chọn)

    // Chỉ vẽ khi zoom đủ lớn để tránh rối mắt
    if (zoomFactor < 0.5f) { // Điều chỉnh ngưỡng zoom này nếu cần
         return;
    }

    for (const auto& street : streetWays) {
        if (street.nodeIds.size() < 2) continue; // Cần ít nhất 2 điểm để vẽ

        // Tìm vị trí trung tâm hoặc vị trí phù hợp để đặt tên đường
        // Cách đơn giản: lấy điểm giữa của đoạn dài nhất hoặc điểm giữa của toàn bộ way
        long long nodeId1 = street.nodeIds[street.nodeIds.size() / 2 -1];
        long long nodeId2 = street.nodeIds[street.nodeIds.size() / 2];

        if (osmNodes.count(nodeId1) && osmNodes.count(nodeId2)) {
            const auto& node1 = osmNodes.at(nodeId1);
            const auto& node2 = osmNodes.at(nodeId2);

            if (node1.pixelX < 0 || node2.pixelX < 0) continue; // Node chưa được map tọa độ

            // Tọa độ pixel trung bình trên map
            int mapMidX = (node1.pixelX + node2.pixelX) / 2;
            int mapMidY = (node1.pixelY + node2.pixelY) / 2;

            // Chuyển sang tọa độ màn hình
            int screenX = (int)(mapMidX * zoomFactor) + offsetX;
            int screenY = (int)(mapMidY * zoomFactor) + offsetY;

             // Kiểm tra xem điểm này có nằm trong màn hình không (Clipping cơ bản)
             if (screenX < -100 || screenX > WIDTH + 100 || screenY < -50 || screenY > HEIGHT + 50) {
                 // Đệm rộng hơn một chút để tên không bị mất đột ngột ở mép
                 continue;
             }

            // Render tên đường (có thể cần hàm renderText tùy chỉnh)
            // renderText(street.name, screenX, screenY, streetFont, streetColor);
            // Hoặc dùng hàm renderText cũ nếu không cần tùy chỉnh nhiều
             SDL_Surface* surface = TTF_RenderUTF8_Blended(font, street.name.c_str(), streetColor);
             if(surface) {
                 SDL_Texture* texture = SDL_CreateTextureFromSurface(renderer, surface);
                 if (texture) {
                     SDL_Rect dstRect = { screenX - surface->w / 2, screenY - surface->h / 2, surface->w, surface->h }; // Canh giữa chữ
                     SDL_RenderCopy(renderer, texture, nullptr, &dstRect);
                     SDL_DestroyTexture(texture);
                 }
                 SDL_FreeSurface(surface);
             }
        }
    }
}

// Hàm load OSM (dùng pugixml)
bool loadOsmData(const char* filename, int mapImageWidth, int mapImageHeight) {
    pugi::xml_document doc;
    pugi::xml_parse_result result = doc.load_file(filename);

    if (!result) {
        cerr << "Error parsing OSM file '" << filename << "': " << result.description() << endl;
        return false;
    }

    cout << "Parsing OSM nodes..." << endl;
    for (pugi::xml_node node = doc.child("osm").child("node"); node; node = node.next_sibling("node")) {
        OsmNode osmNode;
        osmNode.id = node.attribute("id").as_llong();
        osmNode.lat = node.attribute("lat").as_double();
        osmNode.lon = node.attribute("lon").as_double();
        osmNodes[osmNode.id] = osmNode;
    }
    cout << "Parsed " << osmNodes.size() << " nodes." << endl;


    cout << "Parsing OSM ways for streets..." << endl;
    int streetCount = 0;
    for (pugi::xml_node way = doc.child("osm").child("way"); way; way = way.next_sibling("way")) {
        bool isHighway = false;
        string wayName = "";
        vector<long long> currentNodeIds;

        for (pugi::xml_node tag = way.child("tag"); tag; tag = tag.next_sibling("tag")) {
            string k = tag.attribute("k").value();
            if (k == "highway") { // Có nhiều loại đường, có thể lọc thêm nếu muốn
                isHighway = true;
            } else if (k == "name") {
                wayName = tag.attribute("v").value();
            }
        }

        if (isHighway && !wayName.empty()) {
            StreetWay street;
            street.name = wayName;
            for (pugi::xml_node nd = way.child("nd"); nd; nd = nd.next_sibling("nd")) {
                street.nodeIds.push_back(nd.attribute("ref").as_llong());
            }
            if (street.nodeIds.size() >= 2) { // Chỉ lưu đường có ít nhất 2 điểm
                streetWays.push_back(street);
                streetCount++;
            }
        }
    }
    cout << "Found " << streetCount << " named street ways." << endl;

    //Chuyển đổi tọa độ sau khi parse ---
    cout << "Mapping OSM coordinates to pixels..." << endl;
    int mappedCount = 0;
    for (auto& pair : osmNodes) {
        OsmNode& node = pair.second;
        SDL_Point p = mapLatLonToPixel(node.lat, node.lon, mapImageWidth, mapImageHeight);
        node.pixelX = p.x;
        node.pixelY = p.y;
        if(node.pixelX >= 0) mappedCount++; // Đếm những node map thành công (nằm trong vùng ảnh)
    }
     cout << "Mapped " << mappedCount << " nodes to pixel coordinates." << endl;

    return true;
}

void input(const SDL_Event& e) {
    if (e.type == SDL_MOUSEBUTTONDOWN && e.button.button == SDL_BUTTON_LEFT) {
        int mouseX = e.button.x;
        int mouseY = e.button.y;

        int mapX = (int)((mouseX - offsetX) / zoomFactor);
        int mapY = (int)((mouseY - offsetY) / zoomFactor);

        if (isStart) {
            pointA = {mapX, mapY};
            inputTextA = to_string(mapX) + "," + to_string(mapY); // cập nhật text luôn
        }
        else {
            pointB = {mapX, mapY};
            inputTextB = to_string(mapX) + "," + to_string(mapY);
        }
    }
    else if (e.type == SDL_TEXTINPUT) {
        if (isStart) {
            inputTextA += e.text.text;
            pointA = parseInputToPoint(inputTextA);
        } else {
            inputTextB += e.text.text;
            pointB = parseInputToPoint(inputTextB);
        }
    }
    else if (e.type == SDL_KEYDOWN) {
        if (e.key.keysym.sym == SDLK_BACKSPACE) {
            if (isStart && !inputTextA.empty()) {
                inputTextA.pop_back();
                pointA = parseInputToPoint(inputTextA);
            } else if (!isStart && !inputTextB.empty()) {
                inputTextB.pop_back();
                pointB = parseInputToPoint(inputTextB);
            }
        }
        else if (e.key.keysym.sym == SDLK_TAB) {
            isStart = !isStart;
        }
    }
}

void closeAll() {
    SDL_DestroyTexture(mapTexture); 
    TTF_CloseFont(font);
    IMG_Quit();
    TTF_Quit();
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    SDL_Quit();
}

int main(int argc, char* argv[]) {
    SDL_Init(SDL_INIT_VIDEO);
    TTF_Init();
    IMG_Init(IMG_INIT_PNG);

    window = SDL_CreateWindow("SDL2 Large Map", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, WIDTH, HEIGHT, SDL_WINDOW_RESIZABLE);
    renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED);
    font = TTF_OpenFont("font/Roboto-Regular.ttf", 20);

    loadMap("MapNgocHa2.png");

    SDL_StartTextInput();
    SDL_Event e;
    bool quit = false;

    int mapWidth, mapHeight;
    SDL_QueryTexture(mapTexture, nullptr, nullptr, &mapWidth, &mapHeight);

    if (!loadOsmData("map.osm", mapWidth, mapHeight)) {
        std::cerr << "Failed to load or process OSM data. Street names will not be available." << std::endl;
        // Quyết định xem có thoát chương trình hay tiếp tục không có tên đường
    }

    // Scale bản đồ để hiển thị toàn bộ vừa cửa sổ
    float scaleX = (float)WIDTH / mapWidth;
    float scaleY = (float)HEIGHT / mapHeight;
    zoomFactor = min(scaleX, scaleY);
    minZoomFactor = zoomFactor;

    // Canh giữa bản đồ
    offsetX = (int)((WIDTH - mapWidth * zoomFactor) / 2.0f);
    offsetY = (int)((HEIGHT - mapHeight * zoomFactor) / 2.0f);

    while (!quit) {
        while (SDL_PollEvent(&e)) {
            // [x] -> quit
            if (e.type == SDL_QUIT) quit = true; 
            // Nếu lăn chuột
            else if (e.type == SDL_MOUSEWHEEL) {
                int x, y;
                SDL_GetMouseState(&x, &y);
                handleZoom(e.wheel.y, x, y);
            }
            // Nếu không thì pan
            else {
                input(e);
                handlePan(e);
            }
        }

        SDL_RenderClear(renderer);

        // Render map mới 
        SDL_Rect dst;
        dst.x = offsetX;
        dst.y = offsetY;
        dst.w = (int)(mapWidth * zoomFactor);
        dst.h = (int)(mapHeight * zoomFactor);
        SDL_RenderCopy(renderer, mapTexture, nullptr, &dst);

        renderStreetNames(renderer, /*streetFont ? streetFont :*/ font, mapWidth, mapHeight);

        int markerOffset = 15; // Khoảng cách giữa marker và chữ

        // Vẽ marker và nhãn cho điểm A (Start)
        if (pointA.x >= 0) {
            drawMarker(pointA, {0, 255, 0, 255});
            int textX = (int)(pointA.x * zoomFactor) + offsetX + markerOffset;
            int textY = (int)(pointA.y * zoomFactor) + offsetY - 5; // Điều chỉnh vị trí Y cho đẹp
            renderText("Start", textX, textY);
        }

        // Vẽ marker và nhãn cho điểm B (Destination)
        if (pointB.x >= 0) {
            drawMarker(pointB, {255, 0, 0, 255});
            int textX = (int)(pointB.x * zoomFactor) + offsetX + markerOffset;
            int textY = (int)(pointB.y * zoomFactor) + offsetY - 5; // Điều chỉnh vị trí Y cho đẹp
            renderText("Destination", textX, textY);
        }

        // textbox, marker
        SDL_Color bA = isStart ? SDL_Color{100, 200, 255, 255} : SDL_Color{80, 80, 80, 255};
        SDL_Color bB = !isStart ? SDL_Color{100, 200, 255, 255} : SDL_Color{80, 80, 80, 255};
        drawTextboxWithShadow(20, 20, 250, 40, 15, bA, {30, 30, 30, 255});
        renderText("Start: " + inputTextA + (isStart ? " |" : ""), 25, 25);
        drawTextboxWithShadow(20, 70, 250, 40, 15, bB, {30, 30, 30, 255});
        renderText("Destination: " + inputTextB + (!isStart ? " |" : ""), 25, 75);
        // if (pointA.x >= 0) drawMarker(pointA, {0, 255, 0, 255});
        // if (pointB.x >= 0) drawMarker(pointB, {255, 0, 0, 255});

        SDL_RenderPresent(renderer);
    }

    closeAll();
    return 0;
}
