#include "PPU466.hpp"
#include "Mode.hpp"

#include <glm/glm.hpp>

#include <string>
#include <vector>


struct Sprite {
	size_t frame = 0;
	size_t actualFrames = 0;
	std::vector<size_t> tileIndices;
	std::vector<size_t> paletteIndices;
	glm::i8vec2 offset;
};

struct Entity {
	uint8_t flags;
	uint16_t x, y;
	std::vector<Sprite> sprites;
	uint8_t animSpeed = 16;
	
	Entity(const std::string& assetName = "test");

	void LoadSprites(const std::string& assetName);
};

struct Player : Entity {

};

struct Background {
	uint16_t width;
	std::vector<uint16_t> tiles;
};

struct Camera {
	int32_t x = 0;
	uint8_t leftThreshold = 85, rightThreshold = 171;
};

struct PlayMode : Mode {
	PlayMode();
	virtual ~PlayMode();

	virtual bool handle_event(SDL_Event const &, glm::uvec2 const &window_size) override;
	virtual void update(float elapsed) override;
	virtual void draw(glm::uvec2 const &drawable_size) override;

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
	Camera camera;

	//----- drawing handled by PPU466 -----

	PPU466 ppu;
};