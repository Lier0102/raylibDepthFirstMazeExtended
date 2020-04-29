/**********************************************************************************************
*
*   raylib 32x32 game/demo competition
*
*   Competition consist in developing a videogame in a 32x32 pixels screen size.
*
*   RULES:
*
*     1) Use only raylib (and included libraries), no external libraries allowed
*     2) The submission should consist of just one source file
*     3) Render your game/demo to a 32x32 pixels render texture,
*        show what you could do with a 32x32 RGB LED matrix!
*     4) No external resources, you CAN only create them programmatically,
*     5) Game/demo can be 2D or 3D, choose wisely
*     5) Shaders (if used) should be included in the source as string (char *)
*        and loaded with LoadShaderCode()
*     6) Code must compile and execute in PLATFORM_DESKTOP (Windows, Linux, macOS)
*
*
*   LICENSE: zlib/libpng
*
*   Copyright (c) 2020 Jose Miguel Rodriguez Chavarri (https://github.com/txesmi/)
*
*   This software is provided "as-is", without any express or implied warranty. In no event
*   will the authors be held liable for any damages arising from the use of this software.
*
*   Permission is granted to anyone to use this software for any purpose, including commercial
*   applications, and to alter it and redistribute it freely, subject to the following restrictions:
*
*     1. The origin of this software must not be misrepresented; you must not claim that you
*     wrote the original software. If you use this software in a product, an acknowledgment
*     in the product documentation would be appreciated but is not required.
*
*     2. Altered source versions must be plainly marked as such, and must not be misrepresented
*     as being the original software.
*
*     3. This notice may not be removed or altered from any source distribution.
*
**********************************************************************************************/

#include "raylib.h"
#include "raymath.h"
#include <string.h> // memset

#define max(a, b) ((a) > (b) ? (a) : (b))
#define min(a, b) ((a) < (b) ? (a) : (b))

int windowWidth  = 0;
int windowHeight = 0;
const int renderWidth  = 512;
const int renderHeight = 512;
const int gameScreenWidth    = 32;
const int gameScreenHeight   = 32;


/********************************************************************************************/

//--------------------------------------------------------------------------------------------
// DEFINES
//--------------------------------------------------------------------------------------------

#define MAZE_VISIBILITY_MAX       30 // depth max into visibility flood
#define MAZE_ROOM_PERCENT         80 // percent of room creation tries
#define MAZE_NEAR_PERCENT         40 // percent of conections of near depth
#define MAZE_CUT_PERCENT          10 // percent of forced dead ends
#define MAZE_ROOM_BONUS_PERCENT   15 // percent of room tiles filled with bonuses
#define MAZE_DEAD_BONUS_PERCENT   60 // percent of dead end tiles filled with bonuses

//--------------------------------------------------------------------------------------------
// MACROS
//--------------------------------------------------------------------------------------------

#define GETCELL(grid, x, y)  ((grid)->cells + (x) + (y) * (grid)->width)
#define MAKEODD(x) ((int)(x) | 1)

//--------------------------------------------------------------------------------------------
// GRID
//--------------------------------------------------------------------------------------------

typedef struct
{
	void *grid;
	int index;
	int posX;
	int posY;
	int type;
	int neighborCount;
	float depth;
	float timeStamp;
	int flags;
} CELL;

typedef struct
{
	int width;
	int height;
	CELL *cells;
	int size;
	int bonus;
	int ptrOffsets4[4];
	int ptrOffsets8[8];
} GRID;

enum CellTypes
{
	UNVISITED = 0,
	WALL,
	START,
	END_TEMP,
	END,
	ROOMCENTER,
	ROOMBORDER,
	DOOR,
	BONUS,
	LAST_COLOR,
	OPEN
};

Color CellColors[] =
{
	{ 255, 0, 0, 255 },
	{ 110, 110, 110, 255 }, // WALL
	{ 15, 15, 15, 255 },
	{ 255, 0, 255, 255 },
	{ 0, 255, 0, 255 }, // END
	{ 15, 15, 15, 255 }, // ROOM CENTER
	{ 20, 20, 20, 255 }, // ROOM BORDER
	{ 180, 120, 20, 255 }, // DOOR
	{ 255, 255, 80, 255 }, // BONUS
	{ 10, 10, 10, 255 }, // CORRIDORS
};

enum CellFlags {
	CFL_INVISIBLE = (1 << 0)
};

enum GridDirections {
	GRID_RIGHT,     
	GRID_UP,     
	GRID_LEFT,    
	GRID_DOWN
};

const int offsets4[4][2] = {
	 1,  0,
	 0, -1,
	-1,  0,
	 0,  1
};

const int offsets8[8][2] = {
	 1,  0,
	 1, -1,
	 0, -1,
	-1, -1,
	-1,  0,
	-1,  1,
	 0,  1,
	 1,  1
};

void GridClear(GRID *_grid) 
{

}

GRID *GridCreate(int _width, int _height)
{
	GRID *_grid = (GRID*)malloc(sizeof(GRID));
	_grid->width = MAKEODD(max(_width, 7));
	_grid->height = MAKEODD(max(_height, 7));
	_grid->size = _grid->width * _grid->height;
	_grid->cells = (CELL*)malloc(sizeof(CELL) * _grid->size);
	_grid->bonus = 0;

	for (int _dir = 0; _dir < 4; _dir += 1) 
	{
		_grid->ptrOffsets4[_dir] = offsets4[_dir][0] + offsets4[_dir][1] * _grid->width;
		_grid->ptrOffsets8[_dir] = offsets8[_dir][0] + offsets8[_dir][1] * _grid->width;
	}
	for (int _dir = 4; _dir < 8; _dir += 1)
		_grid->ptrOffsets8[_dir] = offsets8[_dir][0] + offsets8[_dir][1] * _grid->width;

	for (int _index = 0; _index < _grid->size; _index += 1)
	{
		CELL *_cell = _grid->cells + _index;
		_cell->grid = (void*)_grid;
		_cell->index = _index;
		_cell->posX = _index % _grid->width;
		_cell->posY = _index / _grid->width;
		_cell->type = UNVISITED;
		_cell->flags = CFL_INVISIBLE;
		_cell->depth = 0;
		_cell->timeStamp = 0;
	}

	return _grid;
}

void GridRemove(GRID *_grid) 
{
	free(_grid->cells);
	free(_grid);
}

void GridMazeRoom(GRID *_grid, int _posX, int _posY, int _cellsToEnd, int _count) 
{
	CELL *_cell = GETCELL(_grid, _posX, _posY);
	if (_cell->type == END) // avoid ending cells
		return;
	if (_cell->type == END_TEMP)
		return;

	// get random direction and turn
	int _dir = GetRandomValue(0, 3);
	int _turnSide = 1 + GetRandomValue(0, 1) * 2;

	// look for unvisited neighbors
	int _attempts = 0;
	for (; _attempts < 4; _attempts += 1, _dir = (_dir + _turnSide) % 4) 
	{
		// check the other three corners of the room
		int _nextX = _posX + offsets4[_dir][0] * 2;
		if (_nextX < 0) continue;
		if (_nextX >= _grid->width) continue;
		int _nextY = _posY + offsets4[_dir][1] * 2;
		if (_nextY < 0) continue;
		if (_nextY >= _grid->height) continue;
		CELL *_cellN1 = GETCELL(_grid, _nextX, _nextY);
		if (_cellN1->type > UNVISITED) continue;

		int _dir2 = (_dir + _turnSide) % 4;
		int _nextX2 = _posX + offsets4[_dir2][0] * 2;
		if (_nextX2 < 0) continue;
		if (_nextX2 >= _grid->width) continue;
		int _nextY2 = _posY + offsets4[_dir2][1] * 2;
		if (_nextY2 < 0) continue;
		if (_nextY2 >= _grid->height) continue;
		CELL *_cellN2 = GETCELL(_grid, _nextX2, _nextY2);
		if (_cellN2->type > UNVISITED) continue;

		int _nextX3 = _posX + (offsets4[_dir][0] + offsets4[_dir2][0]) * 2;
		if (_nextX3 < 0) continue;
		if (_nextX3 >= _grid->width) continue;
		int _nextY3 = _posY + (offsets4[_dir][1] + offsets4[_dir2][1]) * 2;
		if (_nextY3 < 0) continue;
		if (_nextY3 >= _grid->height) continue;
		CELL *_cellN3 = GETCELL(_grid, _nextX3, _nextY3);
		if (_cellN3->type > UNVISITED) continue;

		// set as walkable
		int _xLast = _posX + (offsets4[_dir][0] + offsets4[_dir2][0]) * 3;
		int _yLast = _posY + (offsets4[_dir][1] + offsets4[_dir2][1]) * 3;
		int _stepX = _nextX3 - _posX;
		_stepX /= abs(_stepX);
		int _stepY = _nextY3 - _posY;
		_stepY /= abs(_stepY);
		for (int _x = _posX; _x != _xLast; _x += _stepX)
		{
			for (int _y = _posY; _y != _yLast; _y += _stepY)
			{
				CELL *_cellT = GETCELL(_grid, _x, _y);
				if (_cellT->type > START)
					continue;
				_cellT->type = _cellsToEnd;
				for (int _dir2 = 0; _dir2 < 4; _dir2 += 1)
				{
					int _nextX4 = _x + offsets4[_dir2][0] * 2;
					if (_nextX4 < 0) continue;
					if (_nextX4 >= _grid->width) continue;
					int _nextY4 = _y + offsets4[_dir2][1] * 2;
					if (_nextY4 < 0) continue;
					if (_nextY4 >= _grid->height) continue;
					CELL *_cellN4 = GETCELL(_grid, _nextX4, _nextY4);
					if (abs(_cellN4->type - _cellsToEnd) < 3) 
					{
						_nextX4 = _x + offsets4[_dir2][0];
						_nextY4 = _y + offsets4[_dir2][1];
						_cellN4->type = _cellsToEnd;
					}
				}
			}
		}
		if (0 < _count--) 
		{
			_cellsToEnd += 4;
			int _room = GetRandomValue(0, 2);
			switch (_room) {
			case 0: GridMazeRoom(_grid, _nextX, _nextY, _cellsToEnd, _count); break;
			case 1: GridMazeRoom(_grid, _nextX2, _nextY2, _cellsToEnd, _count); break;
			case 2: GridMazeRoom(_grid, _nextX3, _nextY3, _cellsToEnd, _count); break;
			}
		}
		break;
	}
}

CELL *GridMaze(GRID *_grid) {
	// prepare the grid for maze
	// it states walkable cells as UNVISITED (0)
	// walls pattern
	// X X X X X X X 
	// X 0 X 0 X 0 X
	// X X X X X X X
	// X 0 X 0 X 0 X
	// X X X X X X X
	for ( // pair rows
		struct { CELL *c; CELL *cL; int step; } _sY =
		{
			_grid->cells,
			_grid->cells + _grid->size,
			_grid->width * 2
		};
		_sY.c < _sY.cL;
		_sY.c += _grid->width * 2
		) {
		for (
			struct { CELL *c; CELL *cL; } _sX =
			{
				_sX.c = _sY.c,
				_sX.cL = _sX.c + _grid->width
			};
			_sX.c < _sX.cL;
			_sX.c += 1
			) {
			_sX.c->type = WALL;
			_sX.c->flags = CFL_INVISIBLE;
			_sX.c->depth = 0;
		}
	}

	for ( // odd rows
		struct { CELL *c; CELL *cL; int step; } _sY =
		{
			_grid->cells + _grid->width,
			_grid->cells + _grid->size,
			_grid->width * 2
		};
		_sY.c < _sY.cL;
		_sY.c += _sY.step
		) {
		for (
			struct { CELL *c; CELL *cL; int t; } _sX =
			{
				_sX.c = _sY.c,
				_sX.cL = _sX.c + _grid->width,
				WALL
			};
			_sX.c < _sX.cL;
			_sX.c += 1, _sX.t ^= WALL
			) {
			_sX.c->type = _sX.t;
			_sX.c->flags = CFL_INVISIBLE;
			_sX.c->depth = 0;
		}
	}

	_grid->bonus = 0;

	// break the outter square shape of the grid by disabling some cells beside the border
	for (int _x = 1; _x < _grid->width; _x += _grid->width - 3) { // two loops, left and right columns
		for (int _y = 1; _y < _grid->height; _y += 2) { // odd indexed cells
			if (GetRandomValue(0, 1) > 0)
				continue;
			_grid->cells[_x + _y * _grid->width].type = WALL;
		}
	}
	for (int _y = 1; _y < _grid->height; _y += _grid->height - 3) { // two loops, up and down rows
		for (int _x = 1; _x < _grid->width; _x += 2) { // odd indexed cells
			if (GetRandomValue(0, 1) > 0)
				continue;
			_grid->cells[_x + _y * _grid->width].type = WALL;
		}
	}

	// random ending cell
	// odd coordinates
	CELL *_cellEnd = GETCELL(_grid, MAKEODD(GetRandomValue(3, _grid->width - 4)), MAKEODD(GetRandomValue(3, _grid->height - 4)));
	_cellEnd->type = END;
	int _cellsToEnd = OPEN;
	CELL *_cell = _cellEnd;

	while (1)
	{
		// get random direction and turn direction
		int _dir = GetRandomValue(0, 3);
		int _turnSide = 1 + GetRandomValue(0, 1) * 2;

		// look for unvisited neighbor cell
		int _attempts = 0;
		for (; _attempts < 4; _attempts += 1, _dir = (_dir + _turnSide) % 4)
		{
			// random sudden cuts results in more dead ends
			if (_cellsToEnd > OPEN + 6)
			{
				if (GetRandomValue(1, 100) <= MAZE_CUT_PERCENT) {
					_attempts = 4;
					break;
				}
			}

			// check grid bounds
			int _nextX = _cell->posX + offsets4[_dir][0] * 2;
			if (_nextX < 0) continue;
			if (_nextX >= _grid->width) continue;

			int _nextY = _cell->posY + offsets4[_dir][1] * 2;
			if (_nextY < 0) continue;
			if (_nextY >= _grid->height) continue;

			// get nextcell
			CELL *_cellN = _cell + _grid->ptrOffsets4[_dir] * 2;

			// skip already visited cells
			if (_cellN->type > UNVISITED) continue;

			// set next cell as walkable
			_cellN->type = OPEN;

			// set also the cell in the middle
			(_cell + _grid->ptrOffsets4[_dir])->type = OPEN;

			// save the stepped position
			_cell = _cellN;

			// increase the depth
			_cellsToEnd += 2;

			// random rooms
			if (GetRandomValue(1, 100) <= MAZE_ROOM_PERCENT) {
				int _nextX2 = _cell->posX - offsets4[_dir][0] * 2;
				int _nextY2 = _cell->posY - offsets4[_dir][1] * 2;
				GridMazeRoom(_grid, _nextX2, _nextY2, _cellsToEnd, 1);
			}
			break;
		}

		if (_attempts == 4) // if it could not find a walkable neighbor
		{
			if ((_cell->type != END) && (_cell->type != END_TEMP))  // not an end yet
			{
				// go back to an open cell
				_cell->type = _cellsToEnd;
				_cellsToEnd -= 1;
				int _dir2 = 0;
				for (; _dir2 < 4; _dir2 += 1) 
				{
					CELL *_cellN = _cell + _grid->ptrOffsets4[_dir2];
					if (_cellN->type != OPEN)
						continue;
					_cellN->type = _cellsToEnd;
					_cellsToEnd -= 1;
					_cell = _cellN + _grid->ptrOffsets4[_dir2];
					break;
				}
#ifdef _DEBUG
				if (_dir2 == 4)
					vprintf(FormatText("UNKNOWN ERROR: Open cell not found\n       Current depth:%i\n       Cell type: %i", _cellsToEnd, _cell->type));
#endif
			}
			else // end reached
			{ 
				// set temporary ending cells as a common walkable cell
				if (_cell->type == END_TEMP) 
					_cell->type = _cellsToEnd;
				
				// Look for unvisited cells
				int _y = 1;
				for (; _y < _grid->height; _y += 2) 
				{
					int _x = 1;
					for (; _x < _grid->width; _x += 2) 
					{
						CELL *_cellT = GETCELL(_grid, _x, _y);
						if (_cellT->type != UNVISITED) // avoid already visited cells
							continue;
						CELL *_cellN = NULL;
						// check visited neighbors
						int _dir2 = 0;
						for (; _dir2 < 4; _dir2 += 1) 
						{
							int _nextX = _cellT->posX + offsets4[_dir2][0] * 2;
							int _nextY = _cellT->posY + offsets4[_dir2][1] * 2;
							if (_nextX < 0) continue;
							if (_nextX >= _grid->width) continue;
							if (_nextY < 0) continue;
							if (_nextY >= _grid->height) continue;
							_cellN = GETCELL(_grid, _nextX, _nextY);
							if (_cellN->type > OPEN)
								break;
						}
						if (_dir2 != 4)  // walkable neighbor found
						{
							// set the cell in the middle as walkable
							CELL *_cellN2 = _cellT + _grid->ptrOffsets4[_dir2];
							_cellN2->type = _cellN->type + 1; // one step further of found neighbor depth

							// start a new path
							_cell = _cellT;
							_cell->type = END_TEMP; // set as a temporary end cell
							_cellsToEnd = _cellN2->type + 1; // one step further for next cells
							break;
						}
					}
					if (_x < _grid->width)
						break;
				}
				if (_y >= _grid->height)
					break;
			}
		}
	}

	// Connect depth near
	for (int _y = 3; _y < _grid->height - 3; _y += 2) 
	{
		for (int _x = 3; _x < _grid->width - 3; _x += 2) 
		{
			_cell = GETCELL(_grid, _x, _y);
			if (_cell->type < OPEN) // avoid special cells
				continue;
			for (int _dir = 0; _dir < 4; _dir += 1)
			{
				CELL *_cellN = _cell + _grid->ptrOffsets4[_dir] * 2;
				if (_cellN->type < OPEN)
					continue;
				if (GetRandomValue(1, 100) <= MAZE_NEAR_PERCENT)
					continue;
				if (abs(_cellN->type - _cell->type) < 6) // the only working threshold is 6
					(_cell + _grid->ptrOffsets4[_dir])->type = (_cell->type + _cellN->type / 2);
			}
		}
	}

	// Remove isolated wall cells
	for (int _y = 2; _y < _grid->height - 2; _y += 1)
	{
		for (int _x = 2; _x < _grid->width - 2; _x += 1) 
		{
			_cell = GETCELL(_grid, _x, _y);
			if (_cell->type != WALL)
				continue;
			int _cellsToEnd = 0;
			int _dir = 0;
			for (; _dir < 8; _dir += 1) 
			{
				CELL *_cellN = _cell + _grid->ptrOffsets8[_dir];
				if (_cellN->type < START)
					break;
				if ((_cellN->type >= START) && (_cellN->type <= END)) // start and end are navigable cells that has no depth value as ID
					if (_cellsToEnd < OPEN)
						_cellsToEnd = OPEN;
					else
						_cellsToEnd += _cellsToEnd / (_dir + 1); // add the average of already found neighbors
				else
					_cellsToEnd += _cellN->type;
			}
			if (_dir == 8) // if it is surrounded
				_cell->type = _cellsToEnd / 8; // set the depth average of all its neighborhood
		}
	}

	// Look for start point
	CELL *_cellStart = _grid->cells;
	for (int _i = 1; _i < _grid->width / 2; _i += 2) // look on outter cells first, odd indexed cells 
	{
		for (int _x = _i; _x < _grid->width; _x += _grid->width - (_i + 2)) { // two loops, left and right columns
			for (int _y = _i; _y < _grid->height; _y += 2) { // odd indexed cells
				_cell = GETCELL(_grid, _x, _y);
				if (_cell->type > _cellStart->type)
					_cellStart = _cell;
			}
		}
		for (int _y = _i; _y < _grid->height; _y += _grid->height - (_i + 2)) { // two loops, up and down rows
			for (int _x = _i; _x < _grid->width; _x += 2) { // odd indexed cells
				_cell = GETCELL(_grid, _x, _y);
				if (_cell->type > _cellStart->type)
					_cellStart = _cell;
			}
		}
		if (_cellStart != NULL) break; // early break that ensures the starting cell is in the border of the maze
	}
	_cellStart->type = START;

	// count neighbors and identify room centers
	for (int _y = 1; _y < _grid->height - 1; _y += 1) {
		for (int _x = 1; _x < _grid->width - 1; _x += 1) {
			_cell = GETCELL(_grid, _x, _y); 
			_cell->neighborCount = 0;
			if (_cell->type < START)
				continue;
			for (int _dir = 0; _dir < 8; _dir += 1) {
				CELL *_cellN = _cell + _grid->ptrOffsets8[_dir];
				if (_cellN->type < START)
					continue;
				_cell->neighborCount += 1;
			}
			if (_cell->neighborCount == 8)
				_cell->type = ROOMCENTER;
		}
	}

	// identify room borders
	for (int _y = 1; _y < _grid->height - 1; _y += 1) {
		for (int _x = 1; _x < _grid->width - 1; _x += 1) {
			_cell = GETCELL(_grid, _x, _y);
			if (_cell->type < OPEN) // avoid any special cell
				continue;
			for (int _dir = 0; _dir < 8; _dir += 1) {
				CELL *_cellN = _cell + _grid->ptrOffsets8[_dir];
				if (_cellN->type != ROOMCENTER)
					continue;
				_cell->type = ROOMBORDER;
				break;
			}
		}
	}

	// doors around rooms
	for (int _y = 1; _y < _grid->height - 1; _y += 1) {
		for (int _x = 1; _x < _grid->width - 1; _x += 1) {
			_cell = GETCELL(_grid, _x, _y);
			if (_cell->type < OPEN) // avoid any special cell
				continue;
			for (int _dir = 0; _dir < 4; _dir += 1) {
				CELL *_cellN = _cell + _grid->ptrOffsets4[_dir];
				if (_cellN->type != ROOMBORDER)
					continue;
				_cell->type = DOOR;
				break;
			}
		}
	}

	// doors on corridor crosses
	for (int _y = 1; _y < _grid->height - 1; _y += 1) {
		for (int _x = 1; _x < _grid->width - 1; _x += 1) {
			_cell = GETCELL(_grid, _x, _y);
			if (_cell->type < OPEN) // avoid any special cell
				continue;
			int _count = 0;
			CELL *_cellCheapest = _cell;
			int _dir = 0;
			for (; _dir < 4; _dir += 1) {
				CELL *_cellN = _cell + _grid->ptrOffsets4[_dir];
				if (_cellN->type < OPEN)
					continue;
				_count += 1;
				if (_cellN->type >= _cellCheapest->type)
					continue;
				_cellCheapest = _cellN;
			}
			if(_cell != _cellCheapest)
				if (_count > 2)
					_cellCheapest->type = DOOR;
		}
	}

	// corridor treasures
	for (int _y = 1; _y < _grid->height - 1; _y += 2) {
		for (int _x = 1; _x < _grid->width - 1; _x += 2) {
			_cell = GETCELL(_grid, _x, _y);
			if (_cell->type < OPEN) // avoid special cells
				continue;
			if (_cell->neighborCount != 1)
				continue;
			if (GetRandomValue(1, 100) > MAZE_DEAD_BONUS_PERCENT)
				continue;
			_cell->type = BONUS;
			_grid->bonus += 1;
		}
	}

	// room treasures
	for (int _x = 1; _x < _grid->width - 1; _x += 1) {
		for (int _y = 1; _y < _grid->height - 1; _y += 1) {
			_cell = GETCELL(_grid, _x, _y);
			if (_cell->type < ROOMCENTER) // avoid special cells
				continue;
			if (_cell->type > ROOMBORDER) // avoid corridor cells
				continue;
			if (GetRandomValue(1, 100) > MAZE_ROOM_BONUS_PERCENT)
				continue;
			_cell->type = BONUS;
			_grid->bonus += 1;
		}
	}

	if (_grid->bonus == 0)
		return GridMaze(_grid);

	return _cellStart;
}

void GridFloodVisibility(CELL *_cell, float _depth, float _timeStamp) 
{
	// set flood values
	_cell->depth = _depth;
	_cell->timeStamp = _timeStamp;

	// end by depth
	if (_cell->depth < 0) {
		_cell->flags |= CFL_INVISIBLE; // set cell as invisible
		return;
	}
	_depth -= 5 - _cell->neighborCount / 2;

	// set cell as invisible
	_cell->flags &= ~CFL_INVISIBLE;

	// end by visibility blocking cells
	if ((_cell->type <= WALL) || (_cell->type == DOOR))
		return;

	// flood heighborhood
	for (int _dir = 0; _dir < 8; _dir += 1)
	{
		CELL *_cellT = _cell + ((GRID*)_cell->grid)->ptrOffsets8[_dir];
		if((_cellT->timeStamp != _timeStamp) || (_cellT->depth < _depth)) // avoid already computed cells nearer
			GridFloodVisibility(_cellT, _depth, _timeStamp);
	}
}

//--------------------------------------------------------------------------------------------
// SOUND
//--------------------------------------------------------------------------------------------

#define SND_BUF_SIZE               4096
#define SND_SAMPLE_RATE            8000

typedef struct {
	short *wave;
	int samples;
	float length;
	struct SOUND *next;
} SOUND;

SOUND *SoundCreateTone(float _frequency, float _length, float _volume) {
	_length *= (double)SND_SAMPLE_RATE / 11025.0;
	SOUND *_sound = (SOUND*)malloc(sizeof(SOUND));
	int _waveLength = (int)((float)SND_SAMPLE_RATE / _frequency);
	int _waveCount = (int)(min((float)SND_BUF_SIZE, (float)SND_BUF_SIZE * _length) / _waveLength);
	_sound->samples = _waveLength * _waveCount;
	_sound->wave = (short*)malloc(sizeof(short) * _sound->samples);
	for (int _s = 0; _s < _sound->samples; _s += 1)
	{
		int _amplitude = min(_s * 256, 25000 / 4); // attack
		_amplitude = _amplitude * (float)(_sound->samples - _s) / (float)_sound->samples; // decay
		_sound->wave[_s] = (short)(sinf(((2 * PI * (float)_s / _waveLength))) * _amplitude * _volume);
		_sound->wave[_s] += (short)(sinf(((4 * PI * (float)_s / _waveLength))) * _amplitude * _volume * 0.5f); // octave up
		_sound->wave[_s] += (short)(sinf(((8 * PI * (float)_s / _waveLength))) * _amplitude * _volume * 0.25f); // 2 octaves up
	}

	_sound->length = (float)SND_BUF_SIZE * _length / (float)SND_SAMPLE_RATE;

	return _sound;
}

SOUND *SoundCreateNoise(float _length, float _volume) {
	_length *= (double)SND_SAMPLE_RATE / 11025.0;
	SOUND *_sound = (SOUND*)malloc(sizeof(SOUND));
	_sound->samples = (int)min((float)SND_BUF_SIZE, (float)SND_BUF_SIZE * _length);
	_sound->wave = (short*)malloc(sizeof(short) * _sound->samples);
	for (int _s = 0; _s < _sound->samples; _s += 1)
	{
		int _amplitude = min(_s * 256, 25000); // attack
		_amplitude = _amplitude * (float)(_sound->samples - _s) / (float)_sound->samples; // decay
		_sound->wave[_s] = (short)(GetRandomValue(-_amplitude, _amplitude) * _volume);
	}
	_sound->length = (float)_sound->samples / (float)SND_SAMPLE_RATE;
	return _sound;
}

SOUND *SoundCreateHit(float _length, float _volume)
{
	SOUND *_sound = SoundCreateNoise(0.125f, _volume);
	_sound->length = _length;
	return _sound;
}

void SoundRemove(SOUND *_sound)
{
	free(_sound->wave);
	free(_sound);
}

//--------------------------------------------------------------------------------------------
// MELODY
//--------------------------------------------------------------------------------------------

typedef struct {
	SOUND *first;
	SOUND *current;
	AudioStream stream;
	float time;
	struct MELODY *next;
} MELODY;

enum MelodyTypes
{
	MELODY_END,
	MELODY_TONE,
	MELODY_NOISE,
	MELODY_HIT
};

enum MidiKeys {
	MIDI_A0 = 21, 
	MIDI_A0a,
	MIDI_B0,
	MIDI_C1 = 24,
	MIDI_C1a,
	MIDI_D1,
	MIDI_D1a,
	MIDI_E1,
	MIDI_F1,
	MIDI_F1a,
	MIDI_G1,
	MIDI_G1a,
	MIDI_A1,
	MIDI_A1a,
	MIDI_B1,
	MIDI_C2 = 36,
	MIDI_C2a,
	MIDI_D2,
	MIDI_D2a,
	MIDI_E2,
	MIDI_F2,
	MIDI_F2a,
	MIDI_G2,
	MIDI_G2a,
	MIDI_A2,
	MIDI_A2a,
	MIDI_B2,
	MIDI_C3 = 48,
	MIDI_C3a,
	MIDI_D3,
	MIDI_D3a,
	MIDI_E3,
	MIDI_F3,
	MIDI_F3a,
	MIDI_G3,
	MIDI_G3a,
	MIDI_A3,
	MIDI_A3a,
	MIDI_B3,
	MIDI_C4 = 60,
	MIDI_C4a,
	MIDI_D4,
	MIDI_D4a,
	MIDI_E4,
	MIDI_F4,
	MIDI_F4a,
	MIDI_G4,
	MIDI_G4a,
	MIDI_A4,
	MIDI_A4a,
	MIDI_B4,
	MIDI_C5 = 72,
	MIDI_C5a,
	MIDI_D5,
	MIDI_D5a,
	MIDI_E5,
	MIDI_F5,
	MIDI_F5a,
	MIDI_G5,
	MIDI_G5a,
	MIDI_A5,
	MIDI_A5a,
	MIDI_B5,
	MIDI_C6 = 84,
	MIDI_C6a,
	MIDI_D6,
	MIDI_D6a,
	MIDI_E6,
	MIDI_F6,
	MIDI_F6a,
	MIDI_G6,
	MIDI_G6a,
	MIDI_A6,
	MIDI_A6a,
	MIDI_B6,
	MIDI_C7 = 96,
	MIDI_C7a,
	MIDI_D7,
	MIDI_D7a,
	MIDI_E7,
	MIDI_F7,
	MIDI_F7a,
	MIDI_G7,
	MIDI_G7a,
	MIDI_A7,
	MIDI_A7a,
	MIDI_B7,
	MIDI_C8 = 108
};

double FrequencyFromMidi(int _midi) 
{
	return 440.0 * pow(2.0, ((double)_midi - 69.0) / 12.0);
}

MELODY *MelodyCreate(float *_sndDesc)
{
	MELODY *_melody = (MELODY*)malloc(sizeof(MELODY));
	memset(_melody, 0, sizeof(MELODY));

	_melody->stream = InitAudioStream(SND_SAMPLE_RATE, 16, 1);
	_melody->time = 0;
	_melody->next = NULL;

	switch ((int)*_sndDesc)
	{
	case MELODY_TONE:  _melody->first = SoundCreateTone((float)FrequencyFromMidi((int)*(_sndDesc + 1)), *(_sndDesc + 2), *(_sndDesc + 3)); break;
	case MELODY_NOISE: _melody->first = SoundCreateNoise(*(_sndDesc + 2), *(_sndDesc + 3)); break;
	case MELODY_HIT:   _melody->first = SoundCreateHit(*(_sndDesc + 2), *(_sndDesc + 3)); break;
	}
	
	_sndDesc += 4;
	SOUND *_sndPrev = _melody->first;
	for (; *_sndDesc != MELODY_END; _sndDesc += 4)
	{
		switch ((int)*_sndDesc)
		{
		case MELODY_TONE:  _sndPrev->next = SoundCreateTone((float)FrequencyFromMidi((int)*(_sndDesc + 1)), *(_sndDesc + 2), *(_sndDesc + 3)); break;
		case MELODY_NOISE: _sndPrev->next = SoundCreateNoise(*(_sndDesc + 2), *(_sndDesc + 3)); break;
		case MELODY_HIT:   _sndPrev->next = SoundCreateHit(*(_sndDesc + 2), *(_sndDesc + 3)); break;
		}
		_sndPrev = _sndPrev->next;
	}
	_sndPrev->next = NULL;

	return _melody;
}

MELODY *MelodyCreateHit(float *_sndDesc)
{
	MELODY *_melody = (MELODY*)malloc(sizeof(MELODY));
	memset(_melody, 0, sizeof(MELODY));

	_melody->stream = InitAudioStream(SND_SAMPLE_RATE, 16, 1);
	_melody->time = 0;
	_melody->next = NULL;

	_melody->first = SoundCreateHit(*(_sndDesc + 1), *(_sndDesc + 2));
	_sndDesc += 3;
	SOUND *_sndPrev = _melody->first;
	for (; *_sndDesc != 0; _sndDesc += 3)
	{
		_sndPrev->next = SoundCreateHit(*(_sndDesc + 1), *(_sndDesc + 2));
		_sndPrev = _sndPrev->next;
	}
	_sndPrev->next = NULL;
	return _melody;
}

void MelodyRemove(MELODY *_melody)
{
	CloseAudioStream(_melody->stream);
	SOUND *_sndPrev = _melody->first;
	while (_sndPrev->next)
	{
		SOUND *_snd = _sndPrev->next;
		SoundRemove(_sndPrev);
		_sndPrev = _snd;
	}
	SoundRemove(_sndPrev);
	free(_melody);
}

bool MelodyPlay(MELODY *_melody, float _timeStep)
{
	if (_melody->current == NULL)
	{
		SOUND *_sndT = _melody->current = _melody->first;
		if (IsAudioStreamPlaying(_melody->stream))
			StopAudioStream(_melody->stream);
		UpdateAudioStream(_melody->stream, _sndT->wave, _sndT->samples);
		PlayAudioStream(_melody->stream);
		_melody->time = 0;
	}
	else
	{
		_melody->time += _timeStep;
		if (_melody->time >= _melody->current->length)
		{
			_melody->time -= _melody->current->length;
			SOUND *_sndT = _melody->current = _melody->current->next;
			if (_sndT == NULL)
			{
				return false;
			}
			if (IsAudioStreamPlaying(_melody->stream))
				StopAudioStream(_melody->stream);
			UpdateAudioStream(_melody->stream, _sndT->wave, _sndT->samples);
			PlayAudioStream(_melody->stream);
		}
	}
	return true;
}

void MelodyStop(MELODY *_melody)
{
	if (IsAudioStreamPlaying(_melody->stream))
		StopAudioStream(_melody->stream);
	_melody->current = NULL;
}

bool MelodyLoop(MELODY *_melody, float _timeStep)
{
	if (MelodyPlay(_melody, _timeStep))
		return true;
	MelodyPlay(_melody, _timeStep);
	return false;
}

bool IsMelodyPlaying(MELODY *_melody)
{
	return _melody->current != NULL;
}


//--------------------------------------------------------------------------------------------
// GAME
//--------------------------------------------------------------------------------------------

// melody descriptions
// measure 8:8
float melodyHighDesc[] = 
{
	MELODY_TONE, MIDI_C5, 2.0f, 0.0f, // silence
	MELODY_TONE, MIDI_C5, 2.0f, 0.1f,
	MELODY_TONE, MIDI_B4, 3.0f, 0.1f,
	MELODY_TONE, MIDI_B4, 1.0f, 0.1f,

	MELODY_TONE, MIDI_C5, 4.0f, 0.1f,
	MELODY_TONE, MIDI_B4, 5.0f, 0.1f,

	MELODY_TONE, MIDI_C5, 2.0f, 0.1f, // 1:8 late
	MELODY_TONE, MIDI_B4, 3.0f, 0.1f,
	MELODY_TONE, MIDI_B4, 1.0f, 0.1f,
	MELODY_TONE, MIDI_C5, 1.0f, 0.1f,

	MELODY_TONE, MIDI_G5, 2.0f, 0.1f,
	MELODY_TONE, MIDI_C5, 1.0f, 0.1f,
	MELODY_TONE, MIDI_B4, 7.0f, 0.1f, 

	MELODY_TONE, MIDI_C5, 2.0f, 0.1f, // 2:8 late
	MELODY_TONE, MIDI_B4, 3.0f, 0.1f,
	MELODY_TONE, MIDI_B4, 1.0f, 0.1f,

	MELODY_TONE, MIDI_C5, 4.0f, 0.1f,
	MELODY_TONE, MIDI_B4, 5.0f, 0.1f, 

	MELODY_TONE, MIDI_C5, 2.0f, 0.1f, // 1:8 late
	MELODY_TONE, MIDI_B4, 3.0f, 0.1f,
	MELODY_TONE, MIDI_B4, 1.0f, 0.1f,
	MELODY_TONE, MIDI_C5, 1.0f, 0.1f,

	MELODY_TONE, MIDI_G5, 2.0f, 0.1f,
	MELODY_TONE, MIDI_C5, 1.0f, 0.1f,
	MELODY_TONE, MIDI_B4, 4.0f, 0.1f, // 7:8 syncope
	0
};

float melodyHighEndDesc[] =
{
	MELODY_TONE, MIDI_G3, 0.5f, 0, // silence
	MELODY_TONE, MIDI_G3, 0.5f, 0.3f,
	MELODY_TONE, MIDI_A3, 0.5f, 0.3f,
	MELODY_TONE, MIDI_B3, 0.5f, 0.3f,
	MELODY_TONE, MIDI_C4, 0.5f, 0.3f,
	MELODY_TONE, MIDI_B3, 0.5f, 0.3f,
	MELODY_TONE, MIDI_E4, 0.5f, 0.3f,
	MELODY_TONE, MIDI_G4, 0.5f, 0.3f,
	MELODY_TONE, MIDI_C5, 4.0f, 0.3f,
	0
};

float melodyBassDesc[] =
{
	MELODY_TONE, MIDI_A1, 1.6f, 0.7f,
	MELODY_TONE, MIDI_E2, 1.4f, 0.7f,
	MELODY_TONE, MIDI_A2, 1.0f, 0.5f,

	MELODY_TONE, MIDI_A1, 1.6f, 0.7f,
	MELODY_TONE, MIDI_E2, 1.4f, 0.7f,
	MELODY_TONE, MIDI_A2, 1.0f, 0.5f,

	MELODY_TONE, MIDI_B1, 1.6f, 0.7f,
	MELODY_TONE, MIDI_F2, 1.4f, 0.7f,
	MELODY_TONE, MIDI_B2, 1.0f, 0.5f,

	MELODY_TONE, MIDI_F2, 1.6f, 0.7f,
	MELODY_TONE, MIDI_B2, 1.4f, 0.7f,
	MELODY_TONE, MIDI_E3, 1.0f, 0.5f,
	0
};

float melodyBassEndDesc[] =
{
	MELODY_TONE, MIDI_E1, 1.6f, 0.7f,
	MELODY_TONE, MIDI_F1a, 1.4f, 0.5f,
	MELODY_TONE, MIDI_G1, 1.0f, 0.5f,
	MELODY_TONE, MIDI_A2, 4.0f, 0.7f,
	0
};

float melodyClaveDesc[] =
{
	MELODY_HIT, 0, 0.555f, 0.10f,
	MELODY_HIT, 0, 0.755f, 0.07f,
	MELODY_HIT, 0, 0.555f, 0.15f,
	MELODY_HIT, 0, 0.370f, 0.07f,
	MELODY_HIT, 0, 0.750f, 0.10f,
	0
};

float melodyClaveEndDesc[] =
{
	MELODY_HIT, 0, 0.555f, 0.10f,
	MELODY_HIT, 0, 0.555f, 0.07f,
	MELODY_HIT, 0, 8.755f, 0.15f,
	0
};

float melodyBonusDesc[] =
{
	MELODY_TONE, MIDI_A4, 0.2f, 0.3f,
	MELODY_TONE, MIDI_B4, 0.2f, 0.4f,
	MELODY_TONE, MIDI_C5a, 1.0f, 0.5f,
	0
};

float melodyOpenDesc[] =
{
	MELODY_NOISE, 0, 2.0f, 0.25f,
	0
};

// melodies
MELODY *gMelodyHigh = NULL;
MELODY *gMelodyHighEnd = NULL;
MELODY *gMelodyBass = NULL;
MELODY *gMelodyBassEnd = NULL;
MELODY *gMelodyClave = NULL;
MELODY *gMelodyClaveEnd = NULL;
MELODY *gMelodyBonus = NULL;
MELODY *gMelodyOpen = NULL;

// grid pointers
GRID *gGrid = NULL;
CELL *gCell = NULL; // current cell

#define MOVE_STEP                0.12f
#define SELECTOR_MIN             2
#define SELECTOR_MAX             8
int gSizeSelector = 4;

enum GameStates
{
	GAME_MAIN,
	GAME_WIN,
	GAME_RUN,
	GAME_STATES_COUNT
};

int gState = GAME_MAIN;
float gSpeed = 0;
int gBonus = 0; // collected
float gHudBlink = 0;

void GameInit(void)
{
	SetExitKey(0);

	InitAudioDevice();

	if (IsAudioDeviceReady())
	{
		gMelodyHigh = MelodyCreate(melodyHighDesc);
		gMelodyHighEnd = MelodyCreate(melodyHighEndDesc);
		gMelodyBass = MelodyCreate(melodyBassDesc);
		gMelodyBassEnd = MelodyCreate(melodyBassEndDesc);
		gMelodyClave = MelodyCreate(melodyClaveDesc);
		gMelodyClaveEnd = MelodyCreate(melodyClaveEndDesc);
		gMelodyBonus = MelodyCreate(melodyBonusDesc);
		gMelodyOpen = MelodyCreate(melodyOpenDesc);
	}

}

void GameReset(void)
{
	if(gGrid != NULL)
		GridRemove(gGrid);
	gGrid = NULL;
	gCell = NULL;
	gState = GAME_MAIN;
	gBonus = 0;
	gHudBlink = 0;
	gSpeed = 0;
	MelodyStop(gMelodyOpen);
	MelodyStop(gMelodyBonus);
	MelodyStop(gMelodyClaveEnd);
	MelodyStop(gMelodyClave);
	MelodyStop(gMelodyBassEnd);
	MelodyStop(gMelodyBass);
	MelodyStop(gMelodyHighEnd);
	MelodyStop(gMelodyHigh);
}

void GameClose(void)
{
	MelodyRemove(gMelodyOpen);
	MelodyRemove(gMelodyBonus);
	MelodyRemove(gMelodyClaveEnd);
	MelodyRemove(gMelodyClave);
	MelodyRemove(gMelodyBassEnd);
	MelodyRemove(gMelodyBass);
	MelodyRemove(gMelodyHighEnd);
	MelodyRemove(gMelodyHigh);

	if(gGrid != NULL)
		GridRemove(gGrid);

	CloseAudioDevice();
}

void GameMazeCreate()
{
	float _size = 11 + pow(2, gSizeSelector);
	float _prop = (float)GetRandomValue(7, 13) / 10.0f;
	gGrid = GridCreate(max(_size * _prop, 9), max(_size / _prop, 9));
	gCell = GridMaze(gGrid);
	GridFloodVisibility(gCell, MAZE_VISIBILITY_MAX, (float)GetTime());
	gBonus = 0;
}

void Move(int _dir, float *_speed, float _timeStep)
{
	*_speed += _timeStep;
	CELL *_cell = gCell + gGrid->ptrOffsets4[_dir];
	if (_cell->type <= WALL)
		return;
	if (*_speed > MOVE_STEP)
	{
		*_speed -= MOVE_STEP;
		switch (_cell->type)
		{
		case DOOR:
		{
			if (IsAudioDeviceReady())
			{
				MelodyStop(gMelodyOpen);
				MelodyPlay(gMelodyOpen, _timeStep);
			}

			_cell->type = OPEN;
		} break;

		case BONUS:
		{
			if (IsAudioDeviceReady())
			{
				MelodyStop(gMelodyBonus);
				MelodyPlay(gMelodyBonus, _timeStep);
			}
			_cell->type = OPEN;
			gBonus += 1;
			gCell = _cell;
		} break;

		case END:
		{
			if (gBonus == gGrid->bonus)
				gState = GAME_WIN;
			else
				gHudBlink = 5;
			gCell = _cell;
		} break;

		default:
		{
			gCell = _cell;
		} break;
		}

		GridFloodVisibility(gCell, MAZE_VISIBILITY_MAX, (float)GetTime());
	}
}

bool GameLoop(void) 
{
	static float _speed = 1.0f;
	float _timeStep = GetFrameTime();

	switch (gState) 
	{
	case GAME_RUN:
	{
		// melody
		if (IsAudioDeviceReady())
		{
			if (!MelodyLoop(gMelodyHigh, _timeStep))
			{
				MelodyStop(gMelodyBass); // syncronize
				MelodyStop(gMelodyClave);
			}
			if (!MelodyLoop(gMelodyBass, _timeStep))
				MelodyStop(gMelodyClave); // syncronize
			MelodyLoop(gMelodyClave, _timeStep);

			// fx
			if (IsMelodyPlaying(gMelodyBonus))
				MelodyPlay(gMelodyBonus, _timeStep);
			if (IsMelodyPlaying(gMelodyOpen))
				MelodyPlay(gMelodyOpen, _timeStep);
		}

		// update
		if (IsKeyDown(KEY_UP) || IsKeyDown(KEY_W) || IsKeyDown(KEY_I))
			Move(GRID_UP, &_speed, _timeStep);
		else if (IsKeyDown(KEY_DOWN) || IsKeyDown(KEY_S) || IsKeyDown(KEY_K))
			Move(GRID_DOWN, &_speed, _timeStep);
		else if (IsKeyDown(KEY_RIGHT) || IsKeyDown(KEY_D) || IsKeyDown(KEY_L))
			Move(GRID_RIGHT, &_speed, _timeStep);
		else if (IsKeyDown(KEY_LEFT) || IsKeyDown(KEY_A) || IsKeyDown(KEY_J))
			Move(GRID_LEFT, &_speed, _timeStep);
		else
			_speed = MOVE_STEP;

		// maze
		int _offX = 15 - gCell->posX;
		int _offY = 16 - gCell->posY;
		int _x0 = max(0, gCell->posX - 15);
		int _xL = min(gGrid->width, gCell->posX + 16);
		int _y = max(0, gCell->posY - 16);
		int _yL = min(gGrid->height, gCell->posY + 16);
		for (; _y < _yL; _y += 1)
		{
			for (int _x = _x0; _x < _xL; _x += 1)
			{
				CELL *_cellT = GETCELL(gGrid, _x, _y);
				if (_cellT->flags & CFL_INVISIBLE)
					continue;
				Color *_col = CellColors + (long)min(_cellT->type, LAST_COLOR);
				_col->a = 255 * _cellT->depth / MAZE_VISIBILITY_MAX;
				DrawRectangle(_x + _offX, _y + _offY, 1, 1, *_col);
			}
		}

		// bonus bar
		CellColors[BONUS].a = 255;
		int _bonus = gBonus * 30 / gGrid->bonus;
		if (gHudBlink > 0)
		{
			if((int)gHudBlink % 2 < 1)
				DrawRectangle(31, 0, 1, 32 - _bonus, CellColors[BONUS]);
			else
				DrawRectangle(31, 0, 1, 32 - _bonus, RED);
			gHudBlink -= GetFrameTime() * 5.0f;
		}
		else
		{
			DrawRectangle(31, 0, 1, 32 - _bonus, (Color) { 60, 60, 0, 255 });
		}
		if(gBonus > 0)
		{
			DrawRectangle(31, 31 - _bonus, 1, _bonus, CellColors[BONUS]);
			DrawRectangle(31, 31, 1, 1, CellColors[BONUS]);
			if(gBonus == gGrid->bonus)
				DrawRectangle(31, 0, 1, 1, CellColors[BONUS]);
		}

		// player
		DrawRectangle(gCell->posX + _offX, gCell->posY + _offY, 1, 1, WHITE);

		// escape
		if (IsKeyPressed(KEY_ESCAPE))
			GameReset();

	} break;

	case GAME_MAIN:
	{
		// actions
		if (IsKeyReleased(KEY_UP) || IsKeyReleased(KEY_W) || IsKeyReleased(KEY_I))
		{
			GameMazeCreate();
			gState = GAME_RUN;
		}
		else if (IsKeyPressed(KEY_RIGHT) || IsKeyPressed(KEY_D) || IsKeyPressed(KEY_L))
			gSizeSelector = min(gSizeSelector + 1, SELECTOR_MAX);
		else if (IsKeyPressed(KEY_LEFT) || IsKeyPressed(KEY_A) || IsKeyPressed(KEY_J))
			gSizeSelector = max(gSizeSelector - 1, SELECTOR_MIN);

		// screen
		DrawRectangle(0, 0, 32, 1, WHITE);
		DrawRectangle(0, 31, 32, 1, WHITE);
		DrawRectangle(0, 1, 1, 30, WHITE);
		DrawRectangle(31, 1, 1, 30, WHITE);
		DrawTextEx(GetFontDefault(), "maze", (Vector2) { 2, 32 - GetFontDefault().baseSize }, GetFontDefault().baseSize, 1, WHITE);

		// arrows
		DrawLine(16, 11 - gSizeSelector, 14, 13 - gSizeSelector, WHITE);
		DrawLine(16, 11 - gSizeSelector, 18, 13 - gSizeSelector, WHITE);
		if (gSizeSelector > SELECTOR_MIN)
		{
			DrawLine(11 - gSizeSelector, 15, 13 - gSizeSelector, 13, WHITE);
			DrawLine(11 - gSizeSelector, 15, 13 - gSizeSelector, 17, WHITE);
		}
		else
		{
			DrawLine(11 - gSizeSelector, 15, 13 - gSizeSelector, 13, DARKGRAY);
			DrawLine(11 - gSizeSelector, 15, 13 - gSizeSelector, 17, DARKGRAY);
		}
		if (gSizeSelector < SELECTOR_MAX)
		{
			DrawLine(21 + gSizeSelector, 15, 19 + gSizeSelector, 13, WHITE);
			DrawLine(21 + gSizeSelector, 15, 19 + gSizeSelector, 17, WHITE);
		}
		else
		{
			DrawLine(21 + gSizeSelector, 15, 19 + gSizeSelector, 13, DARKGRAY);
			DrawLine(21 + gSizeSelector, 15, 19 + gSizeSelector, 17, DARKGRAY);
		}

		// maze size
		int _minX = 16 - gSizeSelector;
		int _minY = 15 - gSizeSelector;
		int _maxX = _minX + gSizeSelector * 2 - 1;
		int _maxY = _minY + gSizeSelector * 2 - 1;
		int _size = gSizeSelector * 2 - 1;

		DrawRectangle(_minX + 1, _minY, _size, 1, WHITE);
		DrawRectangle(_minX, _maxY, _size, 1, WHITE);
		DrawRectangle(_minX, _minY, 1, _size, WHITE);
		DrawRectangle(_maxX, _minY + 1, 1, _size, WHITE);

		for (int _x = 0; _x < gSizeSelector - SELECTOR_MIN; _x += 1)
		{
			DrawRectangle(_minX + 2 + _x * 2, _maxY - 2, 1, 1, WHITE);
		}

		// escape
		if (IsKeyPressed(KEY_ESCAPE))
			return false;

	} break;

	case GAME_WIN:
	{
		// melody
		if (IsAudioDeviceReady())
		{
			MelodyPlay(gMelodyHighEnd, _timeStep);
			MelodyPlay(gMelodyClaveEnd, _timeStep);
			if (!MelodyPlay(gMelodyBassEnd, _timeStep))
				GameReset();
		}
		else
		{
			GameReset();
		}

		// screen
		DrawRectangle(0, 0, 32, 1, WHITE);
		DrawRectangle(0, 31, 32, 1, WHITE);
		DrawRectangle(0, 1, 1, 30, WHITE);
		DrawRectangle(31, 1, 1, 30, WHITE);
		DrawTextEx(GetFontDefault(), "you", (Vector2) { 2, 32 - GetFontDefault().baseSize * 2 }, GetFontDefault().baseSize, 1, WHITE);
		DrawTextEx(GetFontDefault(), "win", (Vector2) { 2, 32 - GetFontDefault().baseSize }, GetFontDefault().baseSize, 1, WHITE);
	
	} break;

	default:
	{
		DrawTextEx(GetFontDefault(), "unknown\nerror", (Vector2) { 2, 23 }, GetFontDefault().baseSize, 1, RED);
		if (IsKeyPressed(KEY_ESCAPE))
			return false;
		break;
	}
	}
	return true;
}

//--------------------------------------------------------------------------------------------
// MAIN
//--------------------------------------------------------------------------------------------

int main(void) {
	SetTraceLogLevel(LOG_WARNING);
	SetConfigFlags(FLAG_VSYNC_HINT | FLAG_WINDOW_UNDECORATED);
	InitWindow(windowWidth, windowHeight, "Random Maze");

	windowWidth = GetScreenWidth();
	windowHeight = GetScreenHeight();
	int _renderX = (windowWidth - renderWidth) / 2;
	int _renderY = (windowHeight - renderHeight) / 2;

	// Render texture initialization, used to hold the rendering
	RenderTexture2D target = LoadRenderTexture(gameScreenWidth, gameScreenHeight);
	SetTextureFilter(target.texture, FILTER_POINT);  // Texture scale filter to use!

	//----------------------------------------------------------------------------------
	GameInit();
	//----------------------------------------------------------------------------------

	SetTargetFPS(60);               // Set our game to run at 60 frames-per-second

	//--------------------------------------------------------------------------------------

	// Main game loop
	while (!WindowShouldClose()) {    // Detect window close button or ESC key
		// Update
		// Compute required framebuffer scaling
		float scale = min((float)renderWidth / gameScreenWidth, (float)renderHeight / gameScreenHeight);
		//----------------------------------------------------------------------------------

		// Draw
		//----------------------------------------------------------------------------------
		BeginDrawing();
		ClearBackground(BLACK);

		// Draw everything in the render texture, note this will not be rendered on screen, yet
		BeginTextureMode(target);
		ClearBackground(BLACK);         // Clear render texture background color

		//----------------------------------------------------------------------------------
		if (!GameLoop())
		{
			EndTextureMode();
			EndDrawing();
			break;
		}
		//----------------------------------------------------------------------------------

		EndTextureMode();

		// Draw render texture to window, properly scaled
		int _x = (windowWidth - renderWidth) / 2.0f;
		int _y = (windowHeight - renderHeight) / 2.0f;
		DrawTexturePro(
			target.texture,
			(Rectangle) { 
				0, 0, 
				(float)target.texture.width, (float)-target.texture.height 
			},
			(Rectangle) {
				_renderX, _renderY,
				(float)gameScreenWidth*scale, (float)gameScreenHeight*scale
			}, 
			(Vector2) { 0, 0 }, 
			0.0f, 
			WHITE
		);

		// Draw the grid like "stencil" is drawn over the squares to make them look not at all like LEDs!
		for (int x = -1; x < renderWidth; x += 16) DrawRectangle(x + _renderX, _renderY, 2, renderHeight, BLACK);
		for (int y = -1; y < renderHeight; y += 16) DrawRectangle(_renderX, y + _renderY, renderWidth, 2, BLACK);

		EndDrawing();

		//--------------------------------------------------------------------------------------
	}

	// De-Initialization
	//--------------------------------------------------------------------------------------

	//----------------------------------------------------------------------------------
	GameClose();
	//----------------------------------------------------------------------------------

	UnloadRenderTexture(target);    // Unload render texture

	CloseWindow();                  // Close window and OpenGL context

	//--------------------------------------------------------------------------------------

	return 0;
}

