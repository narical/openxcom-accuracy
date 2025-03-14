/*
 * Copyright 2010-2016 OpenXcom Developers.
 *
 * This file is part of OpenXcom.
 *
 * OpenXcom is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * OpenXcom is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with OpenXcom.  If not, see <http://www.gnu.org/licenses/>.
 */
#include "TextList.h"
#include <cstdarg>
#include <cmath>
#include <algorithm>
#include "../Engine/Action.h"
#include "../Engine/Font.h"
#include "../Engine/Palette.h"
#include "../Engine/Options.h"
#include "ArrowButton.h"
#include "ComboBox.h"
#include "ScrollBar.h"
#include "../fmath.h"

namespace OpenXcom
{

/**
 * Sets up a blank list with the specified size and position.
 * @param width Width in pixels.
 * @param height Height in pixels.
 * @param x X position in pixels.
 * @param y Y position in pixels.
 */
TextList::TextList(int width, int height, int x, int y) : InteractiveSurface(width, height, x, y),
	_big(0), _small(0), _font(0), _lang(nullptr), _scroll(0), _visibleRows(0), _selRow(0), _color(0), _color2(0),
	_dot(false), _dotFirstColumn(false), _selectable(false), _condensed(false), _contrast(false), _wrap(false), _flooding(false), _ignoreSeparators(false),
	_bg(0), _selector(0), _margin(0), _scrolling(true), _arrowPos(-1), _scrollPos(4), _arrowType(ARROW_VERTICAL),
	_leftClick(0), _leftPress(0), _leftRelease(0), _rightClick(0), _rightPress(0), _rightRelease(0),
	_arrowsLeftEdge(0), _arrowsRightEdge(0), _noScrollLeftEdge(0), _noScrollRightEdge(0), _comboBox(0)
{
	_up = new ArrowButton(ARROW_BIG_UP, 13, 14, getX() + getWidth() + _scrollPos, getY());
	_up->setVisible(false);
	_up->setTextList(this);
	_down = new ArrowButton(ARROW_BIG_DOWN, 13, 14, getX() + getWidth() + _scrollPos, getY() + getHeight() - 14);
	_down->setVisible(false);
	_down->setTextList(this);
	int h = std::max(_down->getY() - _up->getY() - _up->getHeight(), 1);
	_scrollbar = new ScrollBar(_up->getWidth(), h, getX() + getWidth() + _scrollPos, _up->getY() + _up->getHeight());
	_scrollbar->setVisible(false);
	_scrollbar->setTextList(this);
}

/**
 * Deletes all the stuff contained in the list.
 */
TextList::~TextList()
{
	for (auto& vec : _texts)
	{
		for (auto* text : vec)
		{
			delete text;
		}
	}
	for (auto* ab : _arrowLeft)
	{
		delete ab;
	}
	for (auto* ab : _arrowRight)
	{
		delete ab;
	}
	delete _selector;
	delete _up;
	delete _down;
	delete _scrollbar;
}

/**
 * Changes the position of the surface in the X axis.
 * @param x X position in pixels.
 */
void TextList::setX(int x)
{
	Surface::setX(x);
	_up->setX(getX() + getWidth() + _scrollPos);
	_down->setX(getX() + getWidth() + _scrollPos);
	_scrollbar->setX(getX() + getWidth() + _scrollPos);
	if (_selector != 0)
		_selector->setX(getX());
}

/**
 * Changes the position of the surface in the Y axis.
 * @param y Y position in pixels.
 */
void TextList::setY(int y)
{
	Surface::setY(y);
	_up->setY(getY());
	_down->setY(getY() + getHeight() - 14);
	_scrollbar->setY(_up->getY() + _up->getHeight());
	if (_selector != 0)
		_selector->setY(getY());
}

/**
 * Gets the arrowsLeftEdge.
 * @return arrowsLeftEdge.
 */
int TextList::getArrowsLeftEdge()
{
	return _arrowsLeftEdge;
}

/**
 * Gets the arrowsRightEdge.
 * @return arrowsRightEdge.
 */
int TextList::getArrowsRightEdge()
{
	return _arrowsRightEdge;
}

/**
 * Unpresses all the arrow buttons.
 * @param state Pointer to running state.
 */
void TextList::unpress(State *state)
{
	InteractiveSurface::unpress(state);
	for (auto* ab : _arrowLeft)
	{
		ab->unpress(state);
	}
	for (auto* ab : _arrowRight)
	{
		ab->unpress(state);
	}
}

/**
 * Changes the color of a specific Text object in the list.
 * @param row Row number.
 * @param column Column number.
 * @param color Text color.
 */
void TextList::setCellColor(size_t row, size_t column, Uint8 color)
{
	_texts[row][column]->setColor(color);
	_redraw = true;
}

/**
 * Changes the text color of a whole row in the list.
 * @param row Row number.
 * @param color Text color.
 */
void TextList::setRowColor(size_t row, Uint8 color)
{
	for (auto* text : _texts[row])
	{
		text->setColor(color);
	}
	_redraw = true;
}

/**
 * Returns the text of a specific Text object in the list.
 * @param row Row number.
 * @param column Column number.
 * @return Text string.
 */
std::string TextList::getCellText(size_t row, size_t column) const
{
	return _texts[row][column]->getText();
}

/**
 * Changes the text of a specific Text object in the list.
 * @param row Row number.
 * @param column Column number.
 * @param text Text string.
 */
void TextList::setCellText(size_t row, size_t column, const std::string &text)
{
	_texts[row][column]->setText(text);
	_redraw = true;
}

/**
 * Returns the X position of a specific text column in the list.
 * @param column Column number.
 * @return X position in pixels.
 */
int TextList::getColumnX(size_t column) const
{
	return getX() + _texts[0][column]->getX();
}

/**
 * Returns the Y position of a specific text row in the list.
 * @param row Row number.
 * @return Y position in pixels.
 */
int TextList::getRowY(size_t row) const
{
	return getY() + _texts[row][0]->getY();
}

/**
 * Returns the height of a specific text row in the list.
 * @param row Row number.
 * @return height in pixels.
 */
int TextList::getTextHeight(size_t row) const
{
	return _texts[row].front()->getTextHeight();
}

/**
 * Returns the height of a specific text row in the list.
 * @param row Row number.
 * @return height in pixels.
 */
int TextList::getNumTextLines(size_t row) const
{
	return _texts[row].front()->getNumLines();
}

/**
 * Returns the amount of text rows stored in the list.
 * @return Number of rows.
 */
size_t TextList::getTexts() const
{
	return _texts.size();
}

/**
 * Returns the amount of physical rows stored in the list.
 * @return Number of rows.
 */
size_t TextList::getRowsDoNotUse() const
{
	return _rows.size();
}

/**
 * Gets the index of the last row in the list.
 * @return Index of the last row or -1 if the list is empty.
 */
int TextList::getLastRowIndex() const
{
	return _texts.size() - 1;
}

/**
 * Returns the amount of visible rows stored in the list.
 * @return Number of rows.
 */
size_t TextList::getVisibleRows() const
{
	return _visibleRows;
}

/**
 * Adds a new row of text to the list, automatically creating
 * the required Text objects lined up where they need to be.
 * @param cols Number of columns.
 * @param ... Text for each cell in the new row.
 */
void TextList::addRow(int cols, ...)
{
	va_list args;
	int ncols;
	va_start(args, cols);
	if (cols > 0)
	{
		ncols = cols;
	}
	else
	{
		ncols = 1;
	}

	std::vector<Text*> temp;
	// Positions are relative to list surface.
	int rowX = 0, rowY = 0, rows = 1, rowHeight = 0;
	if (!_texts.empty())
	{
		rowY = _texts.back().front()->getY() + _texts.back().front()->getHeight() + _font->getSpacing();
	}

	for (int i = 0; i < ncols; ++i)
	{
		int width;
		// Place text
		if (_flooding)
		{
			width = 340;
		}
		else
		{
			width = _columns[i];
		}
		Text* txt = new Text(width, _font->getHeight(), _margin + rowX, rowY);
		txt->setPalette(this->getPalette());
		txt->initText(_big, _small, _lang);
		txt->setColor(_color);
		txt->setSecondaryColor(_color2);
		if (_align[i])
		{
			txt->setAlign(_align[i]);
		}
		txt->setHighContrast(_contrast);
		if (_font == _big)
		{
			txt->setBig();
		}
		else
		{
			txt->setSmall();
		}
		if (cols > 0)
			txt->setText(va_arg(args, char*));
		// grab this before we enable word wrapping so we can use it to calculate
		// the total row height below
		int vmargin = _font->getHeight() - txt->getTextHeight();
		// Wordwrap text if necessary
		if (_wrap && txt->getTextWidth() > txt->getWidth())
		{
			txt->setWordWrap(true, true, _ignoreSeparators);
			rows = std::max(rows, txt->getNumLines());
		}
		rowHeight = std::max(rowHeight, txt->getTextHeight() + vmargin);

		// Places dots between text
		if (_dot && (!_dotFirstColumn || i == 0))
		{
			std::string buf = txt->getText();
			unsigned int w = txt->getTextWidth();
			while (w < _columns[i])
			{
				if (_align[i] != ALIGN_RIGHT)
				{
					char fillChar = (i < cols - 1 ? '.' : ' ');
					w += _font->getChar(fillChar).getCrop()->w + _font->getSpacing();
					buf += fillChar;
				}
				if (_align[i] != ALIGN_LEFT)
				{
					char fillChar = (i > 0 ? '.' : ' ');
					w += _font->getChar(fillChar).getCrop()->w + _font->getSpacing();
					buf.insert(0, 1, fillChar);
				}
			}
			txt->setText(buf);
		}

		temp.push_back(txt);
		if (_condensed)
		{
			rowX += txt->getTextWidth();
		}
		else
		{
			rowX += _columns[i];
		}
	}

	// ensure all elements in this row are the same height
	for (int i = 0; i < cols; ++i)
	{
		temp[i]->setHeight(rowHeight);
	}

	_texts.push_back(temp);
	for (int i = 0; i < rows; ++i)
	{
		_rows.push_back(_texts.size() - 1);
	}

	// Place arrow buttons
	// Position defined w.r.t. main window, NOT TextList.
	if (_arrowPos != -1)
	{
		ArrowShape shape1, shape2;
		if (_arrowType == ARROW_VERTICAL)
		{
			shape1 = ARROW_SMALL_UP;
			shape2 = ARROW_SMALL_DOWN;
		}
		else
		{
			shape1 = ARROW_SMALL_LEFT;
			shape2 = ARROW_SMALL_RIGHT;
		}
		ArrowButton *a1 = new ArrowButton(shape1, 11, 8, getX() + _arrowPos, getY());
		a1->setListButton();
		a1->setPalette(this->getPalette());
		a1->setColor(_up->getColor());
		a1->onMouseClick(_leftClick, 0);
		a1->onMousePress(_leftPress);
		a1->onMouseRelease(_leftRelease);
		_arrowLeft.push_back(a1);
		ArrowButton *a2 = new ArrowButton(shape2, 11, 8, getX() + _arrowPos + 12, getY());
		a2->setListButton();
		a2->setPalette(this->getPalette());
		a2->setColor(_up->getColor());
		a2->onMouseClick(_rightClick, 0);
		a2->onMousePress(_rightPress);
		a2->onMouseRelease(_rightRelease);
		_arrowRight.push_back(a2);
	}

	_redraw = true;
	va_end(args);
	updateArrows();
}

/**
 * Removes the last row from the text list.
 */
void TextList::removeLastRow()
{
	if (!_texts.empty())
	{
		_texts.pop_back();
	}
	if (!_rows.empty())
	{
		size_t toRemove = _rows.back();
		while (!_rows.empty() && _rows.back() == toRemove)
		{
			_rows.pop_back();
		}
	}
	if (_arrowPos != -1)
	{
		if (!_arrowLeft.empty())
		{
			_arrowLeft.pop_back();
		}
		if (!_arrowRight.empty())
		{
			_arrowRight.pop_back();
		}
	}
	_redraw = true;
	updateArrows();
}

/**
 * Changes the columns that the list contains.
 * While rows can be unlimited, columns need to be specified
 * since they can have various widths for lining up the text.
 * @param cols Number of columns.
 * @param ... Width of each column.
 */
void TextList::setColumns(int cols, ...)
{
	va_list args;
	va_start(args, cols);

	_columns.clear();
	for (int i = 0; i < cols; ++i)
	{
		_columns.push_back(va_arg(args, int));
	}

	va_end(args);
}

/**
 * Replaces a certain amount of colors in the palette of all
 * the text contained in the list.
 * @param colors Pointer to the set of colors.
 * @param firstcolor Offset of the first color to replace.
 * @param ncolors Amount of colors to replace.
 */
void TextList::setPalette(const SDL_Color *colors, int firstcolor, int ncolors)
{
	Surface::setPalette(colors, firstcolor, ncolors);
	for (auto& vec : _texts)
	{
		for (auto* text : vec)
		{
			text->setPalette(colors, firstcolor, ncolors);
		}
	}
	for (auto* ab : _arrowLeft)
	{
		ab->setPalette(colors, firstcolor, ncolors);
	}
	for (auto* ab : _arrowRight)
	{
		ab->setPalette(colors, firstcolor, ncolors);
	}
	if (_selector != 0)
	{
		_selector->setPalette(colors, firstcolor, ncolors);
	}
	_up->setPalette(colors, firstcolor, ncolors);
	_down->setPalette(colors, firstcolor, ncolors);
	_scrollbar->setPalette(colors, firstcolor, ncolors);
}

/**
 * Changes the resources for the text in the list
 * and calculates the selector and visible amount of rows.
 * @param big Pointer to large-size font.
 * @param small Pointer to small-size font.
 * @param lang Pointer to current language.
 */
void TextList::initText(Font *big, Font *small, Language *lang)
{
	_big = big;
	_small = small;
	_font = small;
	_lang = lang;

	delete _selector;
	_selector = new Surface(getWidth(), _font->getHeight() + _font->getSpacing(), getX(), getY());
	_selector->setPalette(getPalette());
	_selector->setVisible(false);

	updateVisible();

}

/**
 * Changes the height of the text list.
 * @param height New height in pixels.
 */
void TextList::setHeight(int height)
{
	Surface::setHeight(height);
	setY(getY());
	int h = std::max(_down->getY() - _up->getY() - _up->getHeight(), 1);
	_scrollbar->setHeight(h);
	updateVisible();
}

/**
 * Changes the color of the text in the list. This doesn't change
 * the color of existing text, just the color of text added from then on.
 * @param color Color value.
 */
void TextList::setColor(Uint8 color)
{
	_color = color;
	_up->setColor(color);
	_down->setColor(color);
	_scrollbar->setColor(color);
	for (auto& vec : _texts)
	{
		for (auto* text : vec)
		{
			text->setColor(color);
		}
	}
}

/**
 * Returns the color of the text in the list.
 * @return Color value.
 */
Uint8 TextList::getColor() const
{
	return _color;
}

/**
 * Changes the secondary color of the text in the list.
 * @param color Color value.
 */
void TextList::setSecondaryColor(Uint8 color)
{
	_color2 = color;
}

/**
 * Returns the secondary color of the text in the list.
 * @return Color value.
 */
Uint8 TextList::getSecondaryColor() const
{
	return _color2;
}

/**
 * Enables/disables text wordwrapping. When enabled, rows can
 * take up multiple lines of the list, otherwise every row
 * is restricted to one line.
 * @param wrap Wordwrapping setting.
 */
void TextList::setWordWrap(bool wrap)
{
	_wrap = wrap;
}

/**
 * Enables/disables high contrast color. Mostly used for
 * Battlescape text.
 * @param contrast High contrast setting.
 */
void TextList::setHighContrast(bool contrast)
{
	_contrast = contrast;
	for (auto& vec : _texts)
	{
		for (auto* text : vec)
		{
			text->setHighContrast(contrast);
		}
	}
	_scrollbar->setHighContrast(contrast);
}

/**
 * Changes the horizontal alignment of the text in the list. This doesn't change
 * the alignment of existing text, just the alignment of text added from then on.
 * @param align Horizontal alignment.
 * @param col the column to set the alignment for (defaults to -1, meaning "all")
 */
void TextList::setAlign(TextHAlign align, int col)
{
	if (col == -1)
	{
		for (size_t i = 0; i < _columns.size(); ++i)
		{
			_align[i] = align;
		}
	}
	else
	{
		_align[col] = align;
	}
}

/**
 * If enabled, the text in different columns will be separated by dots.
 * Otherwise, it will only be separated by blank space.
 * @param dot True for dots, False for spaces.
 */
void TextList::setDot(bool dot)
{
	_dot = dot;
}

/**
 * If enabled, the text in different columns will be separated by dots.
 * Otherwise, it will only be separated by blank space.
 * @param dot True for dots, False for spaces.
 */
void TextList::setDotFirstColumn(bool dotFirstColumn)
{
	_dotFirstColumn = dotFirstColumn;
}

/**
 * If enabled, the list will respond to player input,
 * highlighting selected rows and receiving clicks.
 * @param selectable Selectable setting.
 */
void TextList::setSelectable(bool selectable)
{
	_selectable = selectable;
}

/**
 * Changes the text list to use the big-size font.
 */
void TextList::setBig()
{
	_font = _big;

	delete _selector;
	_selector = new Surface(getWidth(), _font->getHeight() + _font->getSpacing(), getX(), getY());
	_selector->setPalette(getPalette());
	_selector->setVisible(false);

	updateVisible();
}

/**
 * Changes the text list to use the small-size font.
 */
void TextList::setSmall()
{
	_font = _small;

	delete _selector;
	_selector = new Surface(getWidth(), _font->getHeight() + _font->getSpacing(), getX(), getY());
	_selector->setPalette(getPalette());
	_selector->setVisible(false);

	updateVisible();
}

/**
 * If enabled, the columns will match the text width.
 * Otherwise, they will have a fixed width.
 * @param condensed True for condensed layout, False for table layout.
 */
void TextList::setCondensed(bool condensed)
{
	_condensed = condensed;
}

/**
 * Returns the currently selected row if the text
 * list is selectable.
 * @return Selected row, -1 if none.
 */
unsigned int TextList::getSelectedRow() const
{
	if (_rows.empty() || _selRow >= _rows.size())
	{
		return -1;
	}
	else
	{
		return _rows[_selRow];
	}
}

/**
 * Changes the surface used to draw the background of the selector.
 * @param bg New background.
 */
void TextList::setBackground(Surface *bg)
{
	_bg = bg;
	_scrollbar->setBackground(_bg);
}

/**
 * Changes the horizontal margin placed around the text.
 * @param margin Margin in pixels.
 */
void TextList::setMargin(int margin)
{
	_margin = margin;
}

/**
 * Returns the margin of the text in the list.
 * @return Margin in pixels.
 */
int TextList::getMargin() const
{
	return _margin;
}

/**
 * Sets the no scroll area of the text list.
 * @param left Left edge of the no scroll area.
 * @param right Right edge of the no scroll area.
 */
void TextList::setNoScrollArea(int left, int right)
{
	_noScrollLeftEdge = left;
	_noScrollRightEdge = right;
}

/**
 * Checks if a given coordinate is inside of the no scroll area of the text list.
 * @param x Coordinate to check.
 */
bool TextList::isInsideNoScrollArea(int x)
{
	if (_noScrollRightEdge > 0)
	{
		if (_noScrollLeftEdge <= x && x <= _noScrollRightEdge)
			return true;
	}
	return false;
}

/**
 * Changes the color of the arrow buttons in the list.
 * @param color Color value.
 */
void TextList::setArrowColor(Uint8 color)
{
	_up->setColor(color);
	_down->setColor(color);
	_scrollbar->setColor(color);
}

/**
 * Sets the position of the column of arrow buttons
 * in the text list.
 * @param pos X in pixels (-1 to disable).
 * @param type Arrow orientation type.
 */
void TextList::setArrowColumn(int pos, ArrowOrientation type)
{
	_arrowPos = pos;
	_arrowType = type;
	if (pos == -1)
	{
		// turned off
		_arrowsLeftEdge = 0;
		_arrowsRightEdge = 0;
		return;
	}
	_arrowsLeftEdge = getX() + _arrowPos;
	_arrowsRightEdge = _arrowsLeftEdge + 12 + 11;
}

/**
 * Sets a function to be called every time the left arrows are mouse clicked.
 * @param handler Action handler.
 */
void TextList::onLeftArrowClick(ActionHandler handler)
{
	_leftClick = handler;
	for (auto* ab : _arrowLeft)
	{
		ab->onMouseClick(handler, 0);
	}
}

/**
 * Sets a function to be called every time the left arrows are mouse pressed.
 * @param handler Action handler.
 */
void TextList::onLeftArrowPress(ActionHandler handler)
{
	_leftPress = handler;
	for (auto* ab : _arrowLeft)
	{
		ab->onMousePress(handler);
	}
}

/**
 * Sets a function to be called every time the left arrows are mouse released.
 * @param handler Action handler.
 */
void TextList::onLeftArrowRelease(ActionHandler handler)
{
	_leftRelease = handler;
	for (auto* ab : _arrowLeft)
	{
		ab->onMouseRelease(handler);
	}
}

/**
 * Sets a function to be called every time the right arrows are mouse clicked.
 * @param handler Action handler.
 */
void TextList::onRightArrowClick(ActionHandler handler)
{
	_rightClick = handler;
	for (auto* ab : _arrowRight)
	{
		ab->onMouseClick(handler, 0);
	}
}

/**
 * Sets a function to be called every time the right arrows are mouse pressed.
 * @param handler Action handler.
 */
void TextList::onRightArrowPress(ActionHandler handler)
{
	_rightPress = handler;
	for (auto* ab : _arrowRight)
	{
		ab->onMousePress(handler);
	}
}

/**
 * Sets a function to be called every time the right arrows are mouse released.
 * @param handler Action handler.
 */
void TextList::onRightArrowRelease(ActionHandler handler)
{
	_rightRelease = handler;
	for (auto* ab : _arrowRight)
	{
		ab->onMouseRelease(handler);
	}
}

/**
 * Removes all the rows currently stored in the list.
 */
void TextList::clearList()
{
	for (auto& vec : _texts)
	{
		for (auto* text : vec)
		{
			delete text;
		}
		vec.clear();
	}
	scrollUp(true, false);
	_texts.clear();
	_rows.clear();
	_redraw = true;
}

/**
 * Scrolls the text in the list up by one row or to the top.
 * @param toMax If true then scrolls to the top of the list. false => one row up
 * @param scrollByWheel If true then use wheel scroll, otherwise scroll normally.
 */
void TextList::scrollUp(bool toMax, bool scrollByWheel, size_t howMany)
{
	if (!_scrolling)
		return;
	if (_rows.size() > _visibleRows && _scroll > 0)
	{
		if (toMax)
		{
			scrollTo(0);
		}
		else
		{
			if (scrollByWheel)
			{
				scrollTo(_scroll - std::min((size_t)(Options::mousewheelSpeed), _scroll));
			}
			else
			{
				scrollTo(_scroll - std::min(howMany, _scroll));
			}
		}
	}
}

/**
 * Scrolls the text in the list down by one row or to the bottom.
 * @param toMax If true then scrolls to the bottom of the list. false => one row down
 * @param scrollByWheel If true then use wheel scroll, otherwise scroll normally.
 */
void TextList::scrollDown(bool toMax, bool scrollByWheel, size_t howMany)
{
	if (!_scrolling)
		return;
	if (_rows.size() > _visibleRows && _scroll < _rows.size() - _visibleRows)
	{
		if (toMax)
		{
			scrollTo(_rows.size() - _visibleRows);
		}
		else
		{
			if (scrollByWheel)
			{
				scrollTo(_scroll + Options::mousewheelSpeed);
			}
			else
			{
				scrollTo(_scroll + howMany);
			}
		}
	}
}

/**
 * Updates the visibility of the arrow buttons according to
 * the current scroll position.
 */
void TextList::updateArrows()
{
	_up->setVisible(_rows.size() > _visibleRows /*&& _scroll > 0*/);
	_down->setVisible(_rows.size() > _visibleRows /*&& _scroll < _rows.size() - _visibleRows*/);
	_scrollbar->setVisible(_rows.size() > _visibleRows);
	_scrollbar->invalidate();
	_scrollbar->blit(this->getSurface());
}

/**
 * Updates the amount of visible rows according to the
 * current list and font size.
 */
void TextList::updateVisible()
{
	_visibleRows = 0;
	for (int y = 0; y < getHeight(); y += _font->getHeight() + _font->getSpacing())
	{
		_visibleRows++;
	}
	updateArrows();
}

/**
 * Changes whether the list can be scrolled.
 * @param scrolling True to allow scrolling, false otherwise.
 * @param scrollPos Custom X position for the scroll buttons.
 */
void TextList::setScrolling(bool scrolling, int scrollPos)
{
	_scrolling = scrolling;
	if (scrollPos != _scrollPos)
	{
		_scrollPos = scrollPos;
		_up->setX(getX() + getWidth() + _scrollPos);
		_down->setX(getX() + getWidth() + _scrollPos);
		_scrollbar->setX(getX() + getWidth() + _scrollPos);
	}
}

/**
 * Draws the text list and all the text contained within.
 */
void TextList::draw()
{
	Surface::draw();
	int y = 0;
	if (!_rows.empty())
	{
		// for wrapped items, offset the draw height above the visible surface
		// so that the correct row appears at the top
		for (int row = _scroll; row > 0 && _rows[row] == _rows[row - 1]; --row)
		{
			y -= _font->getHeight() + _font->getSpacing();
		}
		for (size_t i = _rows[_scroll]; i < _texts.size() && i < _rows[_scroll] + _visibleRows; ++i)
		{
			for (auto* text : _texts[i])
			{
				text->setY(y);
				text->blit(this->getSurface());
			}
			if (!_texts[i].empty())
			{
				y += _texts[i].front()->getHeight() + _font->getSpacing();
			}
			else
			{
				y += _font->getHeight() + _font->getSpacing();
			}
		}
	}
}

/**
 * Blits the text list and selector.
 * @param surface Pointer to surface to blit onto.
 */
void TextList::blit(SDL_Surface *surface)
{
	if (_visible && !_hidden)
	{
		_selector->blit(surface);
	}
	Surface::blit(surface);
	if (_visible && !_hidden)
	{
		if (_arrowPos != -1 && !_rows.empty())
		{
			int y = getY();
			for (int row = _scroll; row > 0 && _rows[row] == _rows[row - 1]; --row)
			{
				y -= _font->getHeight() + _font->getSpacing();
			}
			int maxY = getY() + getHeight();
			for (size_t i = _rows[_scroll]; i < _texts.size() && i < _rows[_scroll] + _visibleRows && y < maxY; ++i)
			{
				_arrowLeft[i]->setY(y);
				_arrowRight[i]->setY(y);

				if (y >= getY())
				{
					// only blit arrows that belong to texts that have their first row on-screen
					_arrowLeft[i]->blit(surface);
					_arrowRight[i]->blit(surface);
				}

				if (!_texts[i].empty())
				{
					y += _texts[i].front()->getHeight() + _font->getSpacing();
				}
				else
				{
					y += _font->getHeight() + _font->getSpacing();
				}
			}
		}
		_up->blit(surface);
		_down->blit(surface);
		_scrollbar->blit(surface);
	}
}

/**
 * Passes events to arrow buttons.
 * @param action Pointer to an action.
 * @param state State that the action handlers belong to.
 */
void TextList::handle(Action *action, State *state)
{
	InteractiveSurface::handle(action, state);
	_up->handle(action, state);
	_down->handle(action, state);
	_scrollbar->handle(action, state);
	if (_arrowPos != -1 && !_rows.empty())
	{
		size_t startArrowIdx = _rows[_scroll];
		if (0 < _scroll && _rows[_scroll] == _rows[_scroll - 1])
		{
			// arrows for first partially-visible line of text are off-screen; don't process them
			++startArrowIdx;
		}
		size_t endArrowIdx = _rows[_scroll] + 1;
		size_t endRow = std::min(_rows.size(), _scroll + _visibleRows);
		for (size_t i = _scroll + 1; i < endRow; ++i)
		{
			if (_rows[i] != _rows[i - 1])
			{
				++endArrowIdx;
			}
		}
		for (size_t i = startArrowIdx; i < endArrowIdx; ++i)
		{
			_arrowLeft[i]->handle(action, state);
			_arrowRight[i]->handle(action, state);
		}
	}
	// scrolling by keyboard
	if (action->getDetails()->type == SDL_KEYDOWN &&
		_rows.size() > _visibleRows &&
		(state->hasOnlyOneScrollableTextList() || isMouseCursorOverMe(state)))
	{
		if (action->getDetails()->key.keysym.sym == SDLK_PAGEDOWN)
		{
			scrollDown(false, false, _visibleRows);
		}
		else if (action->getDetails()->key.keysym.sym == SDLK_PAGEUP)
		{
			scrollUp(false, false, _visibleRows);
		}
	}
}

bool TextList::isMouseCursorOverMe(State* state) const
{
	if (state->getCursorX() < _x ||
		state->getCursorX() > _x + _width ||
		state->getCursorY() < _y ||
		state->getCursorY() > _y + _height)
	{
		return false;
	}
	return true;
}

/**
 * Passes ticks to arrow buttons.
 */
void TextList::think()
{
	InteractiveSurface::think();
	_up->think();
	_down->think();
	_scrollbar->think();
	for (auto* ab : _arrowLeft)
	{
		ab->think();
	}
	for (auto* ab : _arrowRight)
	{
		ab->think();
	}
}

/**
 * Ignores any mouse clicks that aren't on a row.
 * @param action Pointer to an action.
 * @param state State that the action handlers belong to.
 */
void TextList::mousePress(Action *action, State *state)
{
	bool allowScroll = true;
	if (Options::changeValueByMouseWheel != 0)
	{
		allowScroll = (action->getAbsoluteXMouse() < _arrowsLeftEdge || action->getAbsoluteXMouse() > _arrowsRightEdge);
	}
	if (isInsideNoScrollArea(action->getAbsoluteXMouse()))
	{
		allowScroll = false;
	}
	if (allowScroll)
	{
		if (action->getDetails()->button.button == SDL_BUTTON_WHEELUP) scrollUp(false, true);
		else if (action->getDetails()->button.button == SDL_BUTTON_WHEELDOWN) scrollDown(false, true);
	}
	if (_selectable)
	{
		if (_selRow < _rows.size())
		{
			InteractiveSurface::mousePress(action, state);
		}
	}
	else
	{
		InteractiveSurface::mousePress(action, state);
	}
}

/*
 * Ignores any mouse clicks that aren't on a row.
 * @param action Pointer to an action.
 * @param state State that the action handlers belong to.
 */
void TextList::mouseRelease(Action *action, State *state)
{
	if (_selectable)
	{
		if (_selRow < _rows.size())
		{
			InteractiveSurface::mouseRelease(action, state);
		}
	}
	else
	{
		InteractiveSurface::mouseRelease(action, state);
	}
}

/**
 * Ignores any mouse clicks that aren't on a row.
 * @param action Pointer to an action.
 * @param state State that the action handlers belong to.
 */
void TextList::mouseClick(Action *action, State *state)
{
	if (_selectable)
	{
		if (_selRow < _rows.size())
		{
			InteractiveSurface::mouseClick(action, state);
			if (_comboBox && action->getDetails()->button.button == SDL_BUTTON_LEFT)
			{
				_comboBox->setSelected(_selRow);
				_comboBox->toggle(false, true);
			}
		}
	}
	else
	{
		InteractiveSurface::mouseClick(action, state);
	}
}

/**
 * Selects the row the mouse is over.
 * @param action Pointer to an action.
 * @param state State that the action handlers belong to.
 */
void TextList::mouseOver(Action *action, State *state)
{
	if (_selectable)
	{
		int rowHeight = _font->getHeight() + _font->getSpacing(); //theoretical line height
		_selRow = std::max(0, (int)(_scroll + (int)floor(action->getRelativeYMouse() / (rowHeight * action->getYScale()))));
		if (_selRow < _rows.size())
		{
			Text *selText = _texts[_rows[_selRow]].front();
			int y = getY() + selText->getY();
			int actualHeight = selText->getHeight() + _font->getSpacing(); //current line height
			if (y < getY() || y + actualHeight > getY() + getHeight())
			{
				actualHeight /= 2;
			}
			if (y < getY())
			{
				y = getY();
			}
			if (_selector->getHeight() != actualHeight)
			{
				// resizing doesn't work, but recreating does, so let's do that!
				delete _selector;
				_selector = new Surface(getWidth(), actualHeight, getX(), y);
				_selector->setPalette(getPalette());
			}
			_selector->setY(y);
			_selector->copy(_bg);
			if (_contrast)
			{
				_selector->offsetBlock(-5);
			}
			else if (_comboBox)
			{
				_selector->offset(+1, Palette::backPos);
			}
			else
			{
				_selector->offsetBlock(-10);
			}
			_selector->setVisible(true);
		}
		else
		{
			_selector->setVisible(false);
		}
	}

	InteractiveSurface::mouseOver(action, state);
}

/**
 * Deselects the row.
 * @param action Pointer to an action.
 * @param state State that the action handlers belong to.
 */
void TextList::mouseOut(Action *action, State *state)
{
	if (_selectable)
	{
		_selector->setVisible(false);
	}

	InteractiveSurface::mouseOut(action, state);
}

/*
 * get the scroll depth.
 * @return scroll depth.
 */
size_t TextList::getScroll()
{
	return _scroll;
}

/*
 * set the scroll depth.
 * @param scroll set the scroll depth to this.
 */
void TextList::scrollTo(size_t scroll)
{
	if (!_scrolling)
		return;
	if (_rows.size() <= _visibleRows)
		return;
	_scroll = Clamp(scroll, (size_t)(0), _rows.size() - _visibleRows);
	draw(); // can't just set _redraw here because reasons
	updateArrows();
}

/**
 * Hooks up the button to work as part of an existing combobox,
 * updating the selection when it's pressed.
 * @param comboBox Pointer to combobox.
 */
void TextList::setComboBox(ComboBox *comboBox)
{
	_comboBox = comboBox;
}

/**
 * Gets the combobox that this list is attached to, if any.
 * @return the attached combobox.
 */
ComboBox *TextList::getComboBox() const
{
	return _comboBox;
}

void TextList::setBorderColor(Uint8 color)
{
	_up->setColor(color);
	_down->setColor(color);
	_scrollbar->setColor(color);
}

int TextList::getScrollbarColor()
{
	return _scrollbar->getColor();
}

bool TextList::isScrollbarVisible() const
{
	return _scrollbar->getVisible();
}

void TextList::setFlooding(bool flooding)
{
	_flooding = flooding;
}

void TextList::setIgnoreSeparators(bool ignoreSeparators)
{
	_ignoreSeparators = ignoreSeparators;
}

}
