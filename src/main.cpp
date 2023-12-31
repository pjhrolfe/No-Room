#define SDL_MAIN_HANDLED

#include <iostream>
#include <fstream>
#include <vector>
#include "json.hpp"
#include "SDL.h"
#include "SDL_image.h"
#include "SDL_mixer.h"
#include "SDL_ttf.h"
#include "AudioHandler.h"
#include "BoxCollider.h"
#include "Enemy.h"
#include "Entity.h"
#include "FrameTimer.h"
#include "InputHandler.h"
#include "Vec2.h"
#include "Vec2Int.h"

using string = std::string;
using json = nlohmann::json;

const SDL_Color WHITE = {255, 255, 255, 255};
const int TURRET_VALUE = 5;
const int OBSTACLE_VALUE = 1;

const char* GetAssetFolderPath() {
    const char* platform = SDL_GetPlatform();

    if (strcmp(platform, "Windows") == 0) {
        return "assets/";
    } else if (strcmp(platform, "Mac OS X") == 0) {
        return "../Resources/";
    } else {
        return "";
    }
}

void DrawTextStringToWidth(const string& text, TTF_Font* font, Vec2Int pos, int width, SDL_Renderer* renderer) {
    Vec2Int requestedSize {0, 0};
    TTF_SizeUTF8(font, text.c_str(), &requestedSize.x, &requestedSize.y);
    const double ratio = static_cast<double>(requestedSize.x) / static_cast<double>(requestedSize.y);
    const int height = static_cast<int>(static_cast<float>(width) / ratio);
    SDL_Surface* textSurface = TTF_RenderText_Blended(font, text.c_str(), WHITE);
    SDL_Texture* textTexture = SDL_CreateTextureFromSurface(renderer, textSurface);
    SDL_FreeSurface(textSurface);
    const SDL_Rect rect {pos.x, pos.y, width, height};
    SDL_RenderCopy(renderer, textTexture, nullptr, &rect);
    SDL_DestroyTexture(textTexture);
}

void DrawTextStringToHeight(const string& text, TTF_Font* font, Vec2Int pos, int height, SDL_Renderer* renderer) {
    Vec2Int requestedSize {0, 0};
    TTF_SizeUTF8(font, text.c_str(), &requestedSize.x, &requestedSize.y);
    const double ratio = static_cast<double>(requestedSize.y) / static_cast<double>(requestedSize.x);
    const int width = static_cast<int>(static_cast<float>(height) / ratio);
    SDL_Surface* textSurface = TTF_RenderText_Blended(font, text.c_str(), WHITE);
    SDL_Texture* textTexture = SDL_CreateTextureFromSurface(renderer, textSurface);
    SDL_FreeSurface(textSurface);
    const SDL_Rect rect {pos.x, pos.y, width, height};
    SDL_RenderCopy(renderer, textTexture, nullptr, &rect);
    SDL_DestroyTexture(textTexture);
}

double Lerp(float start, float end, float t) {
    return start + t * (end - start);
}

void SpawnEnemy(std::vector<Enemy>& enemies, const Vec2& pos, SDL_Texture* texture, const Vec2& size, double speed, int id) {
    enemies.emplace_back(id, texture, BoxCollider({pos.x, pos.y}, {size.x, size.y}), speed);
}

void RemoveEnemy(std::vector<Enemy>& enemies, const Enemy& enemy) {
    auto newEnd = std::remove_if(enemies.begin(), enemies.end(),
                                 [&](const Enemy& iterEnemy) {
                                     return enemy == iterEnemy;
                                 });

    enemies.erase(newEnd, enemies.end());
}

enum GroundType {
    DEFAULT_GROUND,
    SAFE_ZONE,
    WALL,
    PARKING_LOT
};

enum EntityType {
    NO_ENTITY,
    TURRET,
    OBSTACLE
};

enum EnemyType {
    VAN,
    PICKUP
};

struct EnemySpawn {
    double spawnTime;
    EnemyType type;
    double startY;
};

struct Cell {
    GroundType ground;
    EntityType entityType;
    Entity* entity = nullptr;
};

void RemoveEntity(Cell** map, std::pair<int, int> entity) {
    Cell& cell = map[entity.first][entity.second];
    if (cell.entityType != NO_ENTITY) {
        delete cell.entity;
        cell.entity = nullptr;
        cell.entityType = NO_ENTITY;
    }
}

bool SetNextSafeZoneCellToParkingLot(Cell** map, int gridWidth, int gridHeight) {
    for (int i = 0; i < gridWidth; i++) {
        for (int j = 0; j < gridHeight; j++) {
            if (map[i][j].ground == SAFE_ZONE) {
                map[i][j].ground = PARKING_LOT;
                return true;
            }
        }
    }

    return false;
}

int main()
{
    bool gameOver = false;
    bool victorious = false;
    int currentHighestEnemyId = 0;
    double gameClock = 0.0f;
    bool gameplayActive = false;
    EntityType currentEntityType = NO_ENTITY;
    int balance = 25;
    const int GRID_WIDTH = 32;
    const int GRID_HEIGHT = 18;
    const Vec2Int boxSize {50, 50};
    Cell** map = static_cast<Cell **>(malloc(sizeof(Cell) * GRID_WIDTH));
    for (int i = 0; i < GRID_WIDTH; i++) {
        map[i] = static_cast<Cell*>(malloc(sizeof(Cell) * GRID_HEIGHT));
    }

    for (int i = 0; i < 4; i++) {
        for (int j = 0; j < GRID_HEIGHT; j++) {
            map[i][j].ground = WALL;
            map[i][j].entityType = NO_ENTITY;
            map[i][j].entity = nullptr;
        }
    }

    for (int i = 4; i < GRID_WIDTH; i++) {
        for (int j = 0; j < GRID_HEIGHT; j++) {
            map[i][j].ground = DEFAULT_GROUND;
            map[i][j].entityType = NO_ENTITY;
            map[i][j].entity = nullptr;
        }
    }

    for (int i = 0; i < GRID_HEIGHT; i++) {
        map[GRID_WIDTH-3][i].ground = WALL;
    }

    BoxCollider wallCollider(boxSize.x * (GRID_WIDTH-3), 0, boxSize.x, boxSize.y * GRID_HEIGHT);

    for (int i = GRID_WIDTH-2; i < GRID_WIDTH; i++) {
        for (int j = 0; j < GRID_HEIGHT; j++) {
            map[i][j].ground = SAFE_ZONE;
        }
    }

    const int TARGET_WIDTH = 1600;
    const int TARGET_HEIGHT = 900;
    const double TARGET_ASPECT_RATIO = static_cast<double>(TARGET_WIDTH) / static_cast<double>(TARGET_HEIGHT);

    const int WINDOW_WIDTH = 1280;
    const int WINDOW_HEIGHT = 720;
    const double WINDOW_ASPECT_RATIO = static_cast<double>(WINDOW_WIDTH) / static_cast<double>(WINDOW_HEIGHT);

    bool aspectRatiosMatch = true;

    if (TARGET_ASPECT_RATIO != WINDOW_ASPECT_RATIO) {
        aspectRatiosMatch = false;
    }

    SDL_Init(SDL_INIT_EVERYTHING);
    IMG_Init(IMG_INIT_PNG);
    TTF_Init();
    Mix_OpenAudio(44100, MIX_DEFAULT_FORMAT, 2, 2048);
    Mix_MasterVolume(static_cast<int>(static_cast<float>(SDL_MIX_MAXVOLUME) * 0.1));

    // Window creation and position in the center of the screen
    SDL_Window* window = SDL_CreateWindow("No Room", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, WINDOW_WIDTH, WINDOW_HEIGHT, SDL_WINDOW_SHOWN);

    // Render creation
    SDL_Renderer* renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED);
    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
    SDL_Texture* renderTexture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_RGBA8888, SDL_TEXTUREACCESS_TARGET, TARGET_WIDTH, TARGET_HEIGHT);

    string wall1Path = GetAssetFolderPath();
    wall1Path += "Wall1.png";
    SDL_Texture* wall1Texture = IMG_LoadTexture(renderer, wall1Path.c_str());

    string floor1Path = GetAssetFolderPath();
    floor1Path += "Floor1.png";
    SDL_Texture* floor1Texture = IMG_LoadTexture(renderer, floor1Path.c_str());

    string floor2Path = GetAssetFolderPath();
    floor2Path += "Floor2.png";
    SDL_Texture* floor2Texture = IMG_LoadTexture(renderer, floor2Path.c_str());

    string parkingLot1Path = GetAssetFolderPath();
    parkingLot1Path += "ParkingLot1.png";
    SDL_Texture* parkingLot1Texture = IMG_LoadTexture(renderer, parkingLot1Path.c_str());

    string parkingLot2Path = GetAssetFolderPath();
    parkingLot2Path += "ParkingLot2.png";
    SDL_Texture* parkingLot2Texture = IMG_LoadTexture(renderer, parkingLot2Path.c_str());

    string turretPath = GetAssetFolderPath();
    turretPath += "Turret.png";
    SDL_Texture* turretTexture = IMG_LoadTexture(renderer, turretPath.c_str());

    string obstacle1Path = GetAssetFolderPath();
    obstacle1Path += "Obstacle1.png";
    SDL_Texture* obstacle1Texture = IMG_LoadTexture(renderer, obstacle1Path.c_str());

    string projectilePath = GetAssetFolderPath();
    projectilePath += "Projectile.png";
    SDL_Texture* projectileTexture = IMG_LoadTexture(renderer, projectilePath.c_str());

    string vanPath = GetAssetFolderPath();
    vanPath += "Van.png";
    SDL_Texture* vanTexture = IMG_LoadTexture(renderer, vanPath.c_str());

    string pickupTruckPath = GetAssetFolderPath();
    pickupTruckPath += "PickupTruck.png";
    SDL_Texture* pickupTruckTexture = IMG_LoadTexture(renderer, pickupTruckPath.c_str());

    string playButtonPath = GetAssetFolderPath();
    playButtonPath += "PlayButton.png";
    SDL_Texture* playButtonTexture = IMG_LoadTexture(renderer, playButtonPath.c_str());

    string pauseButtonPath = GetAssetFolderPath();
    pauseButtonPath += "PauseButton.png";
    SDL_Texture* pauseButtonTexture = IMG_LoadTexture(renderer, pauseButtonPath.c_str());

    string boldFontPath = GetAssetFolderPath();
    boldFontPath += "Changa-Bold.ttf";
    TTF_Font* boldFont = TTF_OpenFont(boldFontPath.c_str(), 120);

    string mediumFontPath = GetAssetFolderPath();
    mediumFontPath += "Changa-Medium.ttf";
    TTF_Font* mediumFont = TTF_OpenFont(mediumFontPath.c_str(), 120);

    string regularFontPath = GetAssetFolderPath();
    regularFontPath += "Changa-Regular.ttf";
    TTF_Font* regularFont = TTF_OpenFont(regularFontPath.c_str(), 120);

    string semiBoldFontPath = GetAssetFolderPath();
    semiBoldFontPath += "Changa-SemiBold.ttf";
    TTF_Font* semiBoldFont = TTF_OpenFont(semiBoldFontPath.c_str(), 120);

    std::vector<string> trackPaths = {

    };

    string alertSoundPath = GetAssetFolderPath();
    alertSoundPath += "Alert.wav";

    string hitEnemySoundPath = GetAssetFolderPath();
    hitEnemySoundPath += "HitEnemy.wav";

    string turretFireSoundPath = GetAssetFolderPath();
    turretFireSoundPath += "TurretFire.wav";

    string placeEntitySoundPath = GetAssetFolderPath();
    placeEntitySoundPath += "PlaceEntity.wav";

    string sellEntitySoundPath = GetAssetFolderPath();
    sellEntitySoundPath += "SellEntity.wav";

    std::vector<string> effectPaths = {
            alertSoundPath,
            hitEnemySoundPath,
            turretFireSoundPath,
            placeEntitySoundPath,
            sellEntitySoundPath
    };

    AudioHandler audioHandler(effectPaths, trackPaths);
    FrameTimer frameTimer;
    InputHandler inputHandler;

    std::vector<Enemy> enemies;
    std::vector<Entity> projectiles;

    SDL_Rect turretButtonRect {25, 100, boxSize.x * 3, boxSize.y * 3};
    BoxCollider turretButtonCollider(turretButtonRect);
    SDL_Rect turretButtonImgRect {turretButtonRect.x + 15, turretButtonRect.y + 15, turretButtonRect.w - 30, turretButtonRect.h - 30};

    SDL_Rect obstacleButtonRect {25, turretButtonRect.y + turretButtonRect.h + 10, boxSize.x * 3, boxSize.y * 3};
    BoxCollider obstacleButtonCollider(obstacleButtonRect);
    SDL_Rect obstacleButtonImgRect {obstacleButtonRect.x + 15, obstacleButtonRect.y + 15, obstacleButtonRect.w - 35, obstacleButtonRect.h - 30};

    SDL_Rect sellButtonRect {25, obstacleButtonRect.y + obstacleButtonRect.h + 10, boxSize.x * 3, boxSize.y};
    BoxCollider sellButtonCollider(sellButtonRect);

    SDL_Rect playButtonRect {25, sellButtonRect.y + sellButtonRect.h + 10, static_cast<int>(boxSize.x * 1.4), boxSize.y};
    BoxCollider playButtonCollider(playButtonRect);
    SDL_Rect playButtonImgRect {playButtonRect.x + 15, playButtonRect.y + 5, playButtonRect.w - 30, playButtonRect.h - 10};

    SDL_Rect pauseButtonRect {25 + playButtonRect.w + 10, sellButtonRect.y + sellButtonRect.h + 10, static_cast<int>(boxSize.x * 1.4), boxSize.y};
    BoxCollider pauseButtonCollider(pauseButtonRect);
    SDL_Rect pauseButtonImgRect {pauseButtonRect.x + 15, pauseButtonRect.y + 5, pauseButtonRect.w - 30, pauseButtonRect.h - 10};

    string enemySpawnPath = GetAssetFolderPath();
    enemySpawnPath += "game.json";
    std::ifstream enemySpawnFile(enemySpawnPath);
    json enemySpawnJson;
    enemySpawnFile >> enemySpawnJson;
    enemySpawnFile.close();

    std::vector<EnemySpawn> enemySpawns;

    for (const auto& item : enemySpawnJson) {
        bool validEnemySpawn = true;

        EnemySpawn enemySpawn{};
        enemySpawn.spawnTime = item["spawn_time"];

        // Convert from seconds to milliseconds
        enemySpawn.spawnTime *= 1000;

        string type = item["type"];

        if (type == "VAN") {
            enemySpawn.type = VAN;
        } else if (type == "PICKUP") {
            enemySpawn.type = PICKUP;
        } else {
            validEnemySpawn = false;
        }

        enemySpawn.startY = item["y"];

        if (validEnemySpawn) {
            enemySpawns.push_back(enemySpawn);
        }
    }

    std::sort(enemySpawns.begin(), enemySpawns.end(), [](const EnemySpawn& a, const EnemySpawn& b) {
       return a.spawnTime < b.spawnTime;
    });

    while (!inputHandler.state.exit) {
        if (enemySpawns.empty()) {
            gameOver = true;
            victorious = true;
        }

        inputHandler.Update();
        Vec2 mouseScalingRatio = {
                static_cast<double>(TARGET_WIDTH) / static_cast<double>(WINDOW_WIDTH),
                static_cast<double>(TARGET_HEIGHT) / static_cast<double>(WINDOW_HEIGHT)
        };

        Vec2Int adjustedMousePos = {
                static_cast<int>(static_cast<double>(inputHandler.state.mousePos.x) * mouseScalingRatio.x),
                static_cast<int>(static_cast<double>(inputHandler.state.mousePos.y) * mouseScalingRatio.y)
        };

        frameTimer.Update();

        if (!gameOver) {
            std::vector<Enemy> enemiesToRemove;
            std::vector<std::pair<int, int>> entitiesToRemove; // By map coordinates

            if (gameplayActive) {
                gameClock += frameTimer.frameDeltaMs;

                EnemySpawn& nextEnemySpawn = enemySpawns.front();
                if (gameClock >= nextEnemySpawn.spawnTime && !enemySpawns.empty()) {
                    if (nextEnemySpawn.type == VAN) {
                        SpawnEnemy(enemies, {-25, nextEnemySpawn.startY}, vanTexture, {static_cast<double>(boxSize.x) * 1.5, static_cast<double>(boxSize.y) * 0.75}, 0.25, currentHighestEnemyId++);
                    } else if (nextEnemySpawn.type == PICKUP) {
                        SpawnEnemy(enemies, {-25, nextEnemySpawn.startY}, pickupTruckTexture, {static_cast<double>(boxSize.x) * 1.5, static_cast<double>(boxSize.y) * 0.75}, 0.25, currentHighestEnemyId++);
                    }

                    enemySpawns.erase(enemySpawns.begin());
                    audioHandler.PlayEffect("Alert");
                }

                for (int i = 0; i < GRID_WIDTH; i++) {
                    for (int j = 0; j < GRID_HEIGHT; j++) {
                        if (map[i][j].entityType != NO_ENTITY) {
                            map[i][j].entity->Update(frameTimer.frameDeltaMs);
                            for (Enemy& enemy : enemies) {
                                if (map[i][j].entity->collider.Intersects(enemy.collider) && !enemy.removed) {
                                    enemiesToRemove.push_back(enemy);
                                    enemy.removed = true;
                                    entitiesToRemove.emplace_back(i, j);
                                    audioHandler.PlayEffect("HitEnemy");
                                }
                            }
                        }
                    }
                }

                for (Enemy& enemy : enemies) {
                    enemy.Update(frameTimer.frameDeltaMs);
                    if (enemy.collider.Intersects(wallCollider)) {
                        std::cout << "Enemy hit wall" << std::endl;

                        // Lose four spaces
                        if (!SetNextSafeZoneCellToParkingLot(map, GRID_WIDTH, GRID_HEIGHT)) {
                            gameOver = true;
                        }

                        if (!SetNextSafeZoneCellToParkingLot(map, GRID_WIDTH, GRID_HEIGHT)) {
                            gameOver = true;
                        }

                        if (!SetNextSafeZoneCellToParkingLot(map, GRID_WIDTH, GRID_HEIGHT)) {
                            gameOver = true;
                        }

                        if (!SetNextSafeZoneCellToParkingLot(map, GRID_WIDTH, GRID_HEIGHT)) {
                            gameOver = true;
                        }

                        enemiesToRemove.push_back(enemy);
                    }
                }

                for (Entity& projectile : projectiles) {
                    projectile.Update(frameTimer.frameDeltaMs);
                    projectile.collider.pos.x -= frameTimer.frameDeltaMs;

                    for (Enemy& enemy : enemies) {
                        if (projectile.collider.Intersects(enemy.collider)) {
                            std::cout << "Projectile hit enemy" << std::endl;
                            enemiesToRemove.push_back(enemy);
                            audioHandler.PlayEffect("HitEnemy");
                        }
                    }
                }
            }

            while (!enemiesToRemove.empty()) {
                balance += 1;
                RemoveEnemy(enemies, enemiesToRemove.front());
                enemiesToRemove.erase(enemiesToRemove.begin());
            }

            while (!entitiesToRemove.empty()) {
                RemoveEntity(map, entitiesToRemove.front());
                entitiesToRemove.erase(entitiesToRemove.begin());
            }

            int currentCellX = adjustedMousePos.x / boxSize.x;
            int currentCellY = adjustedMousePos.y / boxSize.y;
            Cell& currentCell = map[currentCellX][currentCellY];

            if (playButtonCollider.Contains({static_cast<double>(adjustedMousePos.x), static_cast<double>(adjustedMousePos.y)}) && inputHandler.state.leftMousePressedThisFrame) {
                gameplayActive = true;
            }

            if (pauseButtonCollider.Contains({static_cast<double>(adjustedMousePos.x), static_cast<double>(adjustedMousePos.y)}) && inputHandler.state.leftMousePressedThisFrame) {
                gameplayActive = false;
            }

            if (inputHandler.state.leftMousePressedThisFrame && currentCell.ground != WALL) {
                if (currentCell.entityType != NO_ENTITY) {
                    switch (currentEntityType) {
                        case NO_ENTITY:
                            audioHandler.PlayEffect("SellEntity");

                            switch (currentCell.entityType) {
                                case TURRET:
                                    balance += TURRET_VALUE;
                                    delete currentCell.entity;
                                    currentCell.entity = nullptr;
                                    currentCell.entityType = NO_ENTITY;
                                    break;
                                case OBSTACLE:
                                    balance += OBSTACLE_VALUE;
                                    delete currentCell.entity;
                                    currentCell.entity = nullptr;
                                    currentCell.entityType = NO_ENTITY;
                                    break;
                                default:
                                    break;
                            }
                            break;
                        default:
                            break;
                    }
                } else {
                    switch (currentEntityType) {
                        case TURRET:
                            if (balance >= TURRET_VALUE) {
                                audioHandler.PlayEffect("PlaceEntity");
                                currentCell.entity = new TurretEntity({static_cast<double>(currentCellX * boxSize.x), static_cast<double>(currentCellY * boxSize.y)},
                                                                      {static_cast<double>(boxSize.x), static_cast<double>(boxSize.y)},
                                                                      turretTexture,
                                                                      {static_cast<double>(boxSize.x) / 2, static_cast<double>(boxSize.y) / 4},
                                                                      projectileTexture,
                                                                      projectiles,
                                                                      audioHandler);
                                currentCell.entityType = TURRET;
                                balance -= TURRET_VALUE;
                            }
                            break;
                        case OBSTACLE:
                            if (balance >= OBSTACLE_VALUE) {
                                audioHandler.PlayEffect("PlaceEntity");
                                currentCell.entity = new Entity({static_cast<double>(currentCellX * boxSize.x), static_cast<double>(currentCellY * boxSize.y)}, {static_cast<double>(boxSize.x), static_cast<double>(boxSize.y)}, obstacle1Texture);
                                currentCell.entityType = OBSTACLE;
                                balance -= OBSTACLE_VALUE;
                            }
                            break;
                        default:
                            break;
                    }
                }
            }

            SDL_SetRenderTarget(renderer, renderTexture);
            SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
            SDL_RenderClear(renderer);

            SDL_Rect currentlyHoveredCellRect {
                    currentCellX * boxSize.x,
                    currentCellY * boxSize.y,
                    boxSize.x,
                    boxSize.y
            };


            for (int i = 0; i < GRID_WIDTH; i++) {
                for (int j = 0; j < GRID_HEIGHT; j++) {
                    SDL_Rect rect {i * boxSize.x, j * boxSize.y, boxSize.x, boxSize.y};

                    switch (map[i][j].ground) {
                        case DEFAULT_GROUND:
                            SDL_RenderCopy(renderer, floor1Texture, nullptr, &rect);
                            break;
                        case SAFE_ZONE:
                            SDL_RenderCopy(renderer, floor2Texture, nullptr, &rect);
                            break;
                        case WALL:
                            SDL_RenderCopy(renderer, wall1Texture, nullptr, &rect);
                            break;
                        case PARKING_LOT:
                            if (i == GRID_WIDTH-1) {
                                SDL_RenderCopy(renderer, parkingLot1Texture, nullptr, &rect);
                            } else {
                                SDL_RenderCopy(renderer, parkingLot2Texture, nullptr, &rect);
                            }

                            break;
                    }
                }
            }

            if (currentCell.entityType == NO_ENTITY) {
                switch (currentEntityType) {
                    case NO_ENTITY:
                        break;
                    case TURRET:
                        SDL_RenderCopy(renderer, turretTexture, nullptr, &currentlyHoveredCellRect);
                        break;
                    case OBSTACLE:
                        SDL_RenderCopy(renderer, obstacle1Texture, nullptr, &currentlyHoveredCellRect);
                        break;
                }
            }

            for (Entity& projectile : projectiles) {
                projectile.Draw(renderer);
            }

            for (int i = 0; i < GRID_WIDTH; i++) {
                for (int j = 0; j < GRID_HEIGHT; j++) {
                    if (map[i][j].entityType != NO_ENTITY) {
                        map[i][j].entity->Draw(renderer);
                    }
                }
            }

            for (Enemy& enemy : enemies) {
                enemy.Draw(renderer);
            }

            SDL_SetRenderDrawColor(renderer, 255, 0, 0, 255);
            SDL_RenderDrawRect(renderer, &currentlyHoveredCellRect);

            SDL_SetRenderDrawColor(renderer, 128, 128, 128, 255);
            SDL_Rect menuRect {0, 0, boxSize.x * 4, GRID_HEIGHT * boxSize.y};
            SDL_RenderFillRect(renderer, &menuRect);
            DrawTextStringToWidth("No Room", boldFont, {25, 10}, (boxSize.x * 4) - 50, renderer);

            string balanceStr = "$: " + std::to_string(balance);
            DrawTextStringToHeight(balanceStr, regularFont, {25, 50}, boxSize.y, renderer);

            if (turretButtonCollider.Contains({static_cast<double>(adjustedMousePos.x), static_cast<double>(adjustedMousePos.y)}) && inputHandler.state.leftMousePressed) {
                SDL_SetRenderDrawColor(renderer, 144, 144, 144, 255);
                currentEntityType = TURRET;
            } else if (turretButtonCollider.Contains({static_cast<double>(adjustedMousePos.x), static_cast<double>(adjustedMousePos.y)})) {
                SDL_SetRenderDrawColor(renderer, 192, 192, 192, 255);
            } else {
                SDL_SetRenderDrawColor(renderer, 160, 160, 160, 255);
            }

            SDL_RenderFillRect(renderer, &turretButtonRect);
            SDL_RenderCopy(renderer, turretTexture, nullptr, &turretButtonImgRect);
            DrawTextStringToHeight("Turret", regularFont, {turretButtonRect.x + 5, turretButtonRect.y}, 30, renderer);
            DrawTextStringToWidth("$5", regularFont, {turretButtonRect.x + 5, turretButtonRect.y + turretButtonRect.h - 30}, 20, renderer);

            if (obstacleButtonCollider.Contains({static_cast<double>(adjustedMousePos.x), static_cast<double>(adjustedMousePos.y)}) && inputHandler.state.leftMousePressed) {
                SDL_SetRenderDrawColor(renderer, 144, 144, 144, 255);
                currentEntityType = OBSTACLE;
            } else if (obstacleButtonCollider.Contains({static_cast<double>(adjustedMousePos.x), static_cast<double>(adjustedMousePos.y)})) {
                SDL_SetRenderDrawColor(renderer, 192, 192, 192, 255);
            } else {
                SDL_SetRenderDrawColor(renderer, 160, 160, 160, 255);
            }

            SDL_RenderFillRect(renderer, &obstacleButtonRect);
            SDL_RenderCopy(renderer, obstacle1Texture, nullptr, &obstacleButtonImgRect);
            DrawTextStringToHeight("Obstacle", regularFont, {obstacleButtonRect.x + 5, obstacleButtonRect.y}, 30, renderer);
            DrawTextStringToWidth("$1", regularFont, {obstacleButtonRect.x + 5, obstacleButtonRect.y + obstacleButtonRect.h - 35}, 20, renderer);

            if (sellButtonCollider.Contains({static_cast<double>(adjustedMousePos.x), static_cast<double>(adjustedMousePos.y)}) && inputHandler.state.leftMousePressed) {
                SDL_SetRenderDrawColor(renderer, 144, 144, 144, 255);
                currentEntityType = NO_ENTITY;
            } else if (sellButtonCollider.Contains({static_cast<double>(adjustedMousePos.x), static_cast<double>(adjustedMousePos.y)})) {
                SDL_SetRenderDrawColor(renderer, 192, 192, 192, 255);
            } else {
                SDL_SetRenderDrawColor(renderer, 160, 160, 160, 255);
            }

            SDL_RenderFillRect(renderer, &sellButtonRect);
            DrawTextStringToHeight("Sell", regularFont, {sellButtonRect.x + 15, sellButtonRect.y}, sellButtonRect.h, renderer);

            if (playButtonCollider.Contains({static_cast<double>(adjustedMousePos.x), static_cast<double>(adjustedMousePos.y)}) && inputHandler.state.leftMousePressed) {
                SDL_SetRenderDrawColor(renderer, 144, 255, 144, 255);
            } else if (playButtonCollider.Contains({static_cast<double>(adjustedMousePos.x), static_cast<double>(adjustedMousePos.y)})) {
                SDL_SetRenderDrawColor(renderer, 192, 255, 192, 255);
            } else if (gameplayActive) {
                SDL_SetRenderDrawColor(renderer, 160, 255, 160, 255);
            } else {
                SDL_SetRenderDrawColor(renderer, 160, 160, 160, 255);
            }

            SDL_RenderFillRect(renderer, &playButtonRect);
            SDL_RenderCopy(renderer, playButtonTexture, nullptr, &playButtonImgRect);

            if (pauseButtonCollider.Contains({static_cast<double>(adjustedMousePos.x), static_cast<double>(adjustedMousePos.y)}) && inputHandler.state.leftMousePressed) {
                SDL_SetRenderDrawColor(renderer, 255, 144, 144, 255);
            } else if (pauseButtonCollider.Contains({static_cast<double>(adjustedMousePos.x), static_cast<double>(adjustedMousePos.y)})) {
                SDL_SetRenderDrawColor(renderer, 255, 192, 192, 255);
            } else if (!gameplayActive) {
                SDL_SetRenderDrawColor(renderer, 255, 160, 160, 255);
            } else {
                SDL_SetRenderDrawColor(renderer, 160, 160, 160, 255);
            }

            DrawTextStringToHeight("How to play", semiBoldFont, {playButtonRect.x , playButtonRect.y + 150}, 50, renderer);
            DrawTextStringToHeight("Stop the cars", regularFont, {playButtonRect.x , playButtonRect.y + 200}, 37, renderer);
            DrawTextStringToHeight("Kills = $", regularFont, {playButtonRect.x , playButtonRect.y + 225}, 37, renderer);
            DrawTextStringToHeight("Save the grass", regularFont, {playButtonRect.x , playButtonRect.y + 250}, 37, renderer);

            SDL_RenderFillRect(renderer, &pauseButtonRect);
            SDL_RenderCopy(renderer, pauseButtonTexture, nullptr, &pauseButtonImgRect);

            SDL_SetRenderTarget(renderer, nullptr);

            if (aspectRatiosMatch) {
                SDL_RenderCopy(renderer, renderTexture, nullptr, nullptr);
            } else {
                // TODO: Letterbox / pillarbox
                SDL_RenderCopy(renderer, renderTexture, nullptr, nullptr);
            }

            SDL_RenderPresent(renderer);
        } else if (gameOver && !victorious){
            SDL_SetRenderTarget(renderer, renderTexture);
            SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
            SDL_RenderClear(renderer);
            DrawTextStringToHeight("There's no more room", boldFont, {50, 50}, 150, renderer);
            DrawTextStringToHeight("Thanks for playing! :)", regularFont, {50, 150}, 50, renderer);
            DrawTextStringToHeight("peterrolfe.com", regularFont, {50, 200}, 50, renderer);
            SDL_SetRenderTarget(renderer, nullptr);
            SDL_RenderCopy(renderer, renderTexture, nullptr, nullptr);
            SDL_RenderPresent(renderer);
        } else {
            SDL_SetRenderTarget(renderer, renderTexture);
            SDL_SetRenderDrawColor(renderer, 128, 128, 255, 255);
            SDL_RenderClear(renderer);
            DrawTextStringToHeight("You saved the grass! Yay!", boldFont, {50, 50}, 150, renderer);
            DrawTextStringToHeight("Thanks for playing! :)", regularFont, {50, 150}, 50, renderer);
            DrawTextStringToHeight("peterrolfe.com", regularFont, {50, 200}, 50, renderer);
            SDL_SetRenderTarget(renderer, nullptr);
            SDL_RenderCopy(renderer, renderTexture, nullptr, nullptr);
            SDL_RenderPresent(renderer);
        }
    }

    free(map);
    SDL_DestroyTexture(renderTexture);
    SDL_DestroyTexture(wall1Texture);
    SDL_DestroyTexture(floor1Texture);
    SDL_DestroyTexture(floor2Texture);
    SDL_DestroyTexture(parkingLot1Texture);
    SDL_DestroyTexture(parkingLot2Texture);
    SDL_DestroyTexture(turretTexture);
    SDL_DestroyTexture(obstacle1Texture);
    SDL_DestroyTexture(projectileTexture);
    SDL_DestroyTexture(vanTexture);
    SDL_DestroyTexture(pickupTruckTexture);
    SDL_DestroyTexture(playButtonTexture);
    SDL_DestroyTexture(pauseButtonTexture);
    TTF_CloseFont(boldFont);
    TTF_CloseFont(mediumFont);
    TTF_CloseFont(regularFont);
    TTF_CloseFont(semiBoldFont);
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    SDL_Quit();

    return 0;
}