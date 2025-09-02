#include "PPU466.hpp"
#include "Mode.hpp"

#include <glm/glm.hpp>

#include <string>
#include <vector>


struct Sprite {
	size_t frame = 0;
	std::vector<size_t> tileIndices;
	std::vector<size_t> paletteIndices; 
};

struct Entity {
	uint8_t flags;
	uint8_t x, y;
	std::vector<Sprite> sprites;
	
	void loadSprites(const std::string& assetName);
};

struct Player : Entity {

};

struct Background {
	uint16_t width;
	std::vector<uint16_t> tiles;
};

struct PlayMode : Mode {
	PlayMode();
	virtual ~PlayMode();

	virtual bool handle_event(SDL_Event const &, glm::uvec2 const &window_size) override;
	virtual void update(float elapsed) override;
	virtual void draw(glm::uvec2 const &drawable_size) override;

	void LoadAssets();

	void StartLevel(const std::string& levelname);

	//----- game state -----

	//input tracking:
	struct Button {
		uint8_t downs = 0;
		uint8_t pressed = 0;
	} left, right, down, up;


	std::vector<Entity*> entities;

	Player player;
	Background background;

	//----- drawing handled by PPU466 -----

	PPU466 ppu;
};