#include "PlayMode.hpp"
#include "load_save_png.hpp"
#include "data_path.hpp"

#include <random>
#include <iostream>
#include <filesystem>
#include <fstream>
#include <unordered_map>
#include <set>


#define ERROR(msg) std::cerr << "[ERROR] (" << __FILE__ ":" << __LINE__ << ") " << msg << std::endl

std::vector<PPU466::Tile> tiles;
std::vector<PPU466::Palette> palettes;

struct IndexData {
	size_t tileStart, tileEnd;
	size_t paletteIndex;
};
std::unordered_map<std::string, IndexData> spriteData;

struct MapData {
	uint16_t width;
	std::vector<uint16_t> tiles;
};
std::unordered_map<std::string, MapData> mapData;

PlayMode::PlayMode() {
	LoadAssets();

	player.loadSprites("test");
	player.x = 100;
	player.y = 100;
	
	entities.emplace_back(&player);

	StartLevel("level1");
}

PlayMode::~PlayMode() {
}

bool PlayMode::handle_event(SDL_Event const &evt, glm::uvec2 const &window_size) {

	if (evt.type == SDL_EVENT_KEY_DOWN) {
		if (evt.key.key == SDLK_LEFT) {
			left.downs += 1;
			left.pressed = true;
			return true;
		} else if (evt.key.key == SDLK_RIGHT) {
			right.downs += 1;
			right.pressed = true;
			return true;
		} else if (evt.key.key == SDLK_UP) {
			up.downs += 1;
			up.pressed = true;
			return true;
		} else if (evt.key.key == SDLK_DOWN) {
			down.downs += 1;
			down.pressed = true;
			return true;
		}
	} else if (evt.type == SDL_EVENT_KEY_UP) {
		if (evt.key.key == SDLK_LEFT) {
			left.pressed = false;
			return true;
		} else if (evt.key.key == SDLK_RIGHT) {
			right.pressed = false;
			return true;
		} else if (evt.key.key == SDLK_UP) {
			up.pressed = false;
			return true;
		} else if (evt.key.key == SDLK_DOWN) {
			down.pressed = false;
			return true;
		}
	}

	return false;
}

void PlayMode::update(float elapsed) {
	//FIXME: horizontal speed
	constexpr float PlayerSpeed = 30.0f;
	if (left.pressed) player.x -= uint8_t(PlayerSpeed * elapsed);
	if (right.pressed) player.x += uint8_t(PlayerSpeed * elapsed);
	if (down.pressed) player.y -= uint8_t(PlayerSpeed * elapsed);
	if (up.pressed) player.y += uint8_t(PlayerSpeed * elapsed);

	//reset button press counters:
	left.downs = 0;
	right.downs = 0;
	up.downs = 0;
	down.downs = 0;
}

void PlayMode::draw(glm::uvec2 const &drawable_size) {
	//this is all quite retarted i'm realizing now...
	std::set<size_t> usedTiles, usedPalettes;
	
	for (Entity* entity : entities) {
		for (const Sprite& sprite : entity->sprites) {
			usedTiles.insert(sprite.tileIndices[sprite.frame]);
			usedPalettes.insert(sprite.paletteIndices[sprite.frame]);
		}
	}	
	for (const uint16_t& tile : background.tiles) {
		usedTiles.insert(tile & 0xFF);
		usedPalettes.insert((tile >> 8) & 0x07);
	}
	
	std::unordered_map<size_t, size_t> tileMap, paletteMap;
	
	//FIXME: add sorting pass for z order
	size_t index = 0;
	for (size_t globalIndex : usedTiles) {
		ppu.tile_table[index] = tiles[globalIndex];
		tileMap[globalIndex] = index;
		index++;
	}
	index = 0;
	for (size_t globalIndex : usedPalettes) {
		if (index >= 8 ) {
			ERROR("too many palettes");
			return;
		}
		ppu.palette_table[index] = palettes[globalIndex];
		paletteMap[globalIndex] = index;
		index++;
	}
	
	//Sprites
	index = 0;
	for (Entity* entity : entities) {
		for (Sprite& sprite : entity->sprites) {
			//FIXME: add offset vector
			ppu.sprites[index].x = entity->x;
			ppu.sprites[index].y = entity->y;
			ppu.sprites[index].index = uint8_t(tileMap[sprite.tileIndices[sprite.frame]]);
			ppu.sprites[index].attributes = uint8_t(paletteMap[sprite.paletteIndices[sprite.frame]]);
			
			sprite.frame = ++sprite.frame % sprite.tileIndices.size();
			index++;
		}
	}
	for (size_t i = index; i < ppu.sprites.size(); ++i) {
		ppu.sprites[i].y = 255;
	}

	int32_t cameraX = 0;
	int32_t startX = cameraX / 8;
	
	//Background
	for (uint32_t y = 0; y < 30; ++y) {
		for (uint32_t x = 0; x < PPU466::BackgroundWidth; ++x) {
			uint32_t worldX = startX + x;
			uint16_t bgTile = background.tiles[y * background.width + worldX];
			uint16_t tile = uint16_t(tileMap[bgTile & 0xFF] | (paletteMap[(bgTile >> 8) & 0x07] << 8));
			ppu.background[x + PPU466::BackgroundWidth * y] = tile;
		}
	}

	static bool logged = false;
	if (!logged) {
		std::cout << "Background width: " << background.width << std::endl;
		std::cout << "First 10x10 of background tiles:" << std::endl;

		for (uint32_t debugY = 0; debugY < std::min(10u, uint32_t(background.tiles.size() / background.width)); ++debugY) {
			for (uint32_t debugX = 0; debugX < std::min(10u, uint32_t(background.width)); ++debugX) {
			if (debugY * background.width + debugX < background.tiles.size()) {
				uint16_t tile = background.tiles[debugY * background.width + debugX];
				std::cout << (tile & 0xFF) << "(" << ((tile >> 8) & 0x07) << ") ";
			}
			}
			std::cout << std::endl;
		}
		logged = true;
	}
	
	ppu.background_position.x = -(cameraX % 8);
	ppu.background_position.y = 0;

	ppu.draw(drawable_size);
}

//FIXME: move out of constructor
void PlayMode::LoadAssets()
{
	std::string path = data_path("assets");
	for (const auto& itr : std::filesystem::directory_iterator(path + "/sprites")) {
		if (itr.path().extension() != ".png") continue;

		std::vector<glm::u8vec4> data;
		glm::uvec2 size;
		load_png(itr.path().string(), &size, &data, OriginLocation::LowerLeftOrigin);
		
		size_t tileStart = tiles.size();
		size_t paletteIndex = palettes.size();

		for (size_t ty = 0; ty < size.y / 8; ++ty) {
			for (size_t tx = 0; tx < size.x / 8; ++tx) {
				PPU466::Tile tile;
				PPU466::Palette palette; //FIXME: simple additive hash for checking duplicates
				
				std::array<glm::u8vec4, 4> colors;
				size_t count = 0;
				for (size_t y = 0; y < 8; ++y) {
					tile.bit0[y] = 0;
					tile.bit1[y] = 0;

					for (size_t x = 0; x < 8; ++x) {
						glm::u8vec4 color(0, 0, 0, 0);

						size_t index = 0;
						if ((tx * 8 + x )< size.x && (ty * 8 + y) < size.y) {
							color = data[(ty * 8 + y) * size.x + (tx * 8 + x)];
							
							bool found = false;
							for (; index < count; ++index) {
								if (colors[index] == color) {
									found = true;
									break;
								}
							}
							
							if (!found) {
								if (count < 4) {
									colors[count] = color;
									palette[count] = color;
									count++;
								} else {
									ERROR("More than 4 colors in " << itr.path().string());
									continue;
								}
							}
						}

						tile.bit0[y] |= index & 1 ? (1 << x) : 0;
						tile.bit1[y] |= index & 2 ? (1 << x) : 0;
					}
				}
				
				tiles.push_back(tile);
				palettes.push_back(palette);
			}
		}
		
		IndexData& d		= spriteData[itr.path().stem().string()];
		d.tileStart		= tileStart;
		d.tileEnd		= tiles.size();
		d.paletteIndex		= paletteIndex;
		
		std::cout << "Loaded " << itr.path().filename() << ": " << (tiles.size() - tileStart) << " tiles" << std::endl;
	}
	
	std::cout << "Total global tiles: " << tiles.size() << std::endl;
	std::cout << "Total global palettes: " << palettes.size() << std::endl;
	std::cout << "Sprite data entries: ";
	for (const auto& pair : spriteData) {
		std::cout << pair.first << ": " << pair.second.tileEnd << ", ";
	}
	std::cout << std::endl;
	for (const auto& itr : std::filesystem::directory_iterator(path + "/levels")) {
		if (itr.path().extension() != ".map") continue;

		std::ifstream file(itr.path());
		if (!file) {
			throw std::runtime_error("Failed to open .map file '" + itr.path().string() + "'."); 
		}

		MapData& map = mapData[itr.path().stem().string()];

		int counter = 0;
		for (std::string line; std::getline(file, line);) {
			if (line.empty()) continue;

			std::string value;
			for (std::stringstream stream(line); std::getline(stream, value, ',');) {
				value.erase(0, value.find_first_not_of(" \t\n\r"));
				value.erase(value.find_last_not_of(" \t\n\r") + 1);

				auto it = spriteData.find(value);
				if (it == spriteData.end()) {
					ERROR("Tile not found: " << value);
					map.tiles.push_back(0);
					continue;
				}

				uint8_t tile = it->second.tileStart & 0xFF;
				uint8_t palette = it->second.paletteIndex & 0x07;
				map.tiles.push_back(tile | (palette << 8)); //FIXME

				counter++;
			}

			if (counter != -1) {
				map.width = uint16_t(counter);
				counter = -1;
			}
		}

		std::cout << "Loaded " << itr.path().filename() << ": " << map.width << std::endl;
	}
}

void PlayMode::StartLevel(const std::string& levelname)
{
	std::cout << "Starting level: " << levelname << std::endl;
	
	auto itr = mapData.find(levelname);
	if (itr == mapData.end()) {
		ERROR("Level not found: " << levelname);
		return;
	}

	background.width = itr->second.width;
	background.tiles = itr->second.tiles;
	
	std::cout << "Loaded level with width: " << background.width << ", tiles: " << background.tiles.size() << std::endl;
}

void Entity::loadSprites(const std::string& assetName) {
	auto it = spriteData.find(assetName);
	if (it == spriteData.end()) {
		ERROR("Asset not found: " << assetName);
		return;
	}
	
	const IndexData& data = it->second;
	sprites.clear();
	
	for (size_t i = data.tileStart; i < data.tileEnd; ++i) {
		Sprite sprite;
		sprite.frame = 0;
		sprite.tileIndices.emplace_back(i);
		sprite.paletteIndices.emplace_back(data.paletteIndex + (i - data.tileStart));
		sprites.emplace_back(sprite);
	}
}
