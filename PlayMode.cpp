#include "PlayMode.hpp"
#include "load_save_png.hpp"
#include "data_path.hpp"
#include "Load.hpp"

#include <iostream>
#include <filesystem>
#include <fstream>

#include <unordered_map>
#include <set>
#include <algorithm>

#include <random>
#include <regex>


#define ERROR(msg) std::cerr << "[ERROR] (" << __FILE__ ":" << __LINE__ << ") " << msg << std::endl

std::vector<PPU466::Tile> tiles;
std::vector<PPU466::Palette> palettes;

struct SpriteData {
	uint8_t width, height;
	uint8_t frames;
	size_t tileStart;
	std::vector<size_t> paletteIndices;
};
std::unordered_map<std::string, SpriteData> spriteData;

struct MapData {
	uint16_t width;
	std::vector<uint16_t> tiles;
};
std::unordered_map<std::string, MapData> mapData;

Load<void> loadAssets(LoadTagDefault, [](){
	std::cout << std::endl << "=========== LOADING ASSETS ===========" << std::endl;

	std::string path = data_path("assets");

	//there's prob a better way to this but then again u wouldn't need to if ppu could modified...
	auto paletteHash = [](const PPU466::Palette& palette) -> size_t {
		auto colorHash = [](const glm::u8vec4& colors) -> size_t {
			return std::hash<uint32_t>{}(*reinterpret_cast<const uint32_t*>(&colors));
		};
		return colorHash(palette[0]) ^ colorHash(palette[1]) ^ colorHash(palette[2]) ^ colorHash(palette[3]);
	};
	auto paletteEqual = [](const PPU466::Palette& a, const PPU466::Palette& b) -> bool {
		return std::is_permutation(a.begin(), a.end(), b.begin());
	};
	std::unordered_map<PPU466::Palette, size_t, decltype(paletteHash), decltype(paletteEqual)> paletteMap(8, paletteHash, paletteEqual);

	//Collect first because of animation frames
	std::unordered_map<std::string, int8_t> animGroups;

	const std::regex exp("(.*)_(\\d+)\\.png$");
	for (const auto& itr : std::filesystem::directory_iterator(path + "/sprites")) {
		if (itr.path().extension() != ".png") continue;

		std::string filename = itr.path().filename().string();
		std::smatch match;

		if (std::regex_match(filename, match, exp)) {
			std::string base = match[1].str();
			if (animGroups.find(base) == animGroups.end()) {
				animGroups[base] = 0;
			}
			animGroups[base] = std::max(animGroups[base], int8_t(std::stoi(match[2].str())));
		} else {
			animGroups[itr.path().stem().string()] = 0;
		}
	}

	for (const auto& [name, frames] : animGroups) {
		SpriteData& d = spriteData[name];
		d.frames = frames + 1;
		d.tileStart = tiles.size();
		
		std::vector<size_t> paletteIndices;
		for (int8_t i = 0; i <= frames; i++) {
			std::string framePath = path + "/sprites/" + name + ((frames == 0) ? "" : ("_" + std::to_string(i))) + ".png";
			std::vector<glm::u8vec4> data;
			glm::uvec2 size;
			load_png(framePath, &size, &data, OriginLocation::LowerLeftOrigin);

			if (i == 0) {
				d.width = uint8_t(size.x / 8);
				d.height = uint8_t(size.y / 8);
			}
			
			for (size_t ty = 0; ty < size.y / 8; ++ty) {
				for (size_t tx = 0; tx < size.x / 8; ++tx) {
					PPU466::Tile tile;
					PPU466::Palette palette;
					
					std::array<glm::u8vec4, 4> colors;
					size_t count = 0;
					for (size_t y = 0; y < 8; ++y) {
						tile.bit0[y] = 0;
						tile.bit1[y] = 0;
						for (size_t x = 0; x < 8; ++x) {
							if ((tx * 8 + x ) >= size.x && (ty * 8 + y) >= size.y) continue;
							
							glm::u8vec4 color = data[(ty * 8 + y) * size.x + (tx * 8 + x)];
							size_t index = 0;

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
									ERROR("More than 4 colors in " << framePath);
									continue;
								}
							}

							tile.bit0[y] |= index & 1 ? (1 << x) : 0;
							tile.bit1[y] |= index & 2 ? (1 << x) : 0;
						}
					}

					size_t paletteIndex;

					auto it = paletteMap.find(palette);
					if (it == paletteMap.end()) {
						paletteIndex = palettes.size();
						palettes.push_back(palette);
						paletteMap[palette] = paletteIndex;
					} else {
						paletteIndex = it->second;

						const PPU466::Palette& actualPalette = palettes[paletteIndex];
						for (size_t y = 0; y < 8; ++y) {
							tile.bit0[y] = 0;
							tile.bit1[y] = 0;
							for (size_t x = 0; x < 8; ++x) {
								if ((tx * 8 + x ) >= size.x && (ty * 8 + y) >= size.y) continue;

								glm::u8vec4 color = data[(ty * 8 + y) * size.x + (tx * 8 + x)];
								size_t index = 0;						
								for (; index < count; ++index) {
									if (actualPalette[index] == color) break;
								}

								tile.bit0[y] |= index & 1 ? (1 << x) : 0;
								tile.bit1[y] |= index & 2 ? (1 << x) : 0;
							}
						}
					}
					
					tiles.push_back(tile);
					paletteIndices.emplace_back(paletteIndex);
				}
			}
		}
		
		d.paletteIndices = paletteIndices;
		
		std::cout << "Loaded " << name << ": " << (int)d.frames << " frames, " << (tiles.size() - d.tileStart) << " tiles" << std::endl;
	}
	std::cout << "Total tiles: " << tiles.size() << std::endl;
	std::cout << "Total palettes: " << palettes.size() << std::endl;

	for (const auto& itr : std::filesystem::directory_iterator(path + "/levels")) {
		if (itr.path().extension() != ".csv") continue;

		std::ifstream file(itr.path());
		if (!file) {
			throw std::runtime_error("Failed to open .csv file '" + itr.path().string() + "'."); 
		}

		//excel utf8 bom nonsense
		char bom[3];
		file.read(bom, 3);
		if (bom[0] != '\xEF' || bom[1] != '\xBB' || bom[2] != '\xBF') {
			file.seekg(0);
		}

		MapData& map = mapData[itr.path().stem().string()];
		map.width = 0;

		size_t counter = 0;
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
				uint8_t palette = it->second.paletteIndices[0] & 0x07;
				map.tiles.push_back(tile | (palette << 8)); //FIXME

				counter++;
			}

			if (map.width == 0) {
				map.width = uint16_t(counter);
			}
		}

		std::cout << "Loaded " << itr.path().filename() << ": " << map.width << " wide" << std::endl;
	}

	std::cout << "=========== FINISHED LOADING ASSETS ===========" << std::endl;
});

PlayMode::PlayMode() {
	player.LoadSprites("player");
	player.x = 100;
	player.y = 100;
	entities.emplace_back(&player);

	camera.x = std::max(0, int32_t(player.x) - 128);
	
	for (const auto& name : {"dude", "otherdude"}) {
		for (size_t i = rand() % 8; i > 0; i--) {
			Entity* entity = new Entity(name);
			entity->x = uint16_t(rand() % 256);
			entity->y = uint16_t(rand() % 240);
			entities.emplace_back(entity);
		}
	}


	StartLevel("level1");
}

PlayMode::~PlayMode() {
	//FIXME: entity cleanup
}

//FIXME: add WASD
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
	constexpr float PlayerSpeed = 120.0f;
	if (left.pressed) player.x -= uint16_t(PlayerSpeed * elapsed);
	if (right.pressed) player.x += uint16_t(PlayerSpeed * elapsed);
	if (down.pressed) player.y -= uint16_t(PlayerSpeed * elapsed);
	if (up.pressed) player.y += uint16_t(PlayerSpeed * elapsed);

	//camera
	if (int32_t(player.x) - camera.x < camera.leftThreshold) {
		camera.x = int32_t(player.x) - camera.leftThreshold;
	} else if (int32_t(player.x) - camera.x > camera.rightThreshold) {
		camera.x = int32_t(player.x) - camera.rightThreshold;
	}
	camera.x = std::max(0, camera.x);
	camera.x = std::min(camera.x, std::max(0, int32_t(background.width * 8) - 256));

	//reset button press counters:
	left.downs = 0;
	right.downs = 0;
	up.downs = 0;
	down.downs = 0;
}

void PlayMode::draw(glm::uvec2 const &drawable_size) {
	//this is all quite retarded i'm realizing now...
	std::set<size_t> usedTiles, usedPalettes; //FIXME: use vector instead
	
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
			//FIXME: background flashing prob dude to something...
			if (entity->animSpeed != 0) {
				sprite.actualFrames++;
				if (sprite.actualFrames >= entity->animSpeed) {
					sprite.actualFrames = 0;
					sprite.frame = ++sprite.frame % sprite.tileIndices.size();
				}
			}

			int32_t screenX = int32_t(entity->x) + sprite.offset.x - camera.x;
			int32_t screenY = int32_t(entity->y) + sprite.offset.y;

			if (screenX >= -8 && screenX < 256) {
				ppu.sprites[index].x = uint8_t(screenX);
				ppu.sprites[index].y = uint8_t(screenY);
				ppu.sprites[index].index = uint8_t(tileMap[sprite.tileIndices[sprite.frame]]);
				ppu.sprites[index].attributes = uint8_t(paletteMap[sprite.paletteIndices[sprite.frame]]);
				
				index++;
			}
		}
	}
	for (size_t i = index; i < ppu.sprites.size(); ++i) {
		ppu.sprites[i].y = 255; //FIXME: should prob hide under background in case of big sprites maybe?
	}
	
	//Background
	for (uint32_t y = 0; y < 30; ++y) {
		for (uint32_t x = 0; x < PPU466::BackgroundWidth; ++x) {
			uint32_t worldX = camera.x / 8 + x;

			uint16_t tile = 0;
			if (worldX < background.width) {
				uint32_t tileIndex = (29 - y) * background.width + worldX;
				if (tileIndex < background.tiles.size()) {
					tile = background.tiles[tileIndex];

					auto tileItr = tileMap.find(tile & 0xFF);
					auto palItr = paletteMap.find((tile >> 8) & 0x07);
					if (tileItr != tileMap.end() && palItr != paletteMap.end()) {
						tile = uint16_t(tileItr->second | (palItr->second << 8));
					}
				}
			}
			ppu.background[x + PPU466::BackgroundWidth * y] = tile;
		}
	}
	
	ppu.background_position.x = -(camera.x % 8);
	ppu.background_position.y = 0;

	ppu.draw(drawable_size);
}


void PlayMode::StartLevel(const std::string& levelname) {
	auto itr = mapData.find(levelname);
	if (itr == mapData.end()) {
		ERROR("Level not found: " << levelname);
		return;
	}

	background.width = itr->second.width;
	background.tiles = itr->second.tiles;	
}

Entity::Entity(const std::string& assetName) {
	LoadSprites(assetName);
}

void Entity::LoadSprites(const std::string& assetName) {
	auto it = spriteData.find(assetName);
	if (it == spriteData.end()) {
		ERROR("Asset not found: " << assetName);
		return;
	}

	sprites.clear();

	//could be argued we switch the loop order idk...
	const SpriteData& data = it->second;
	for (uint8_t i = 0; i < data.width * data.height; i++) {
		Sprite sprite;
		sprite.offset = data.width > 0 ? glm::i8vec2(i % data.width * 8, i / data.width * 8) : glm::i8vec2(0, 0);

		for (uint8_t frame = 0; frame < data.frames; frame++) {
			sprite.tileIndices.emplace_back(data.tileStart + (frame * data.width * data.height) + i);
			sprite.paletteIndices.emplace_back(data.paletteIndices[frame * data.width * data.height + i]);
		}
		sprites.emplace_back(sprite);
	}
}
