/* Copyright (C) 2010 Wildfire Games.
 * This file is part of 0 A.D.
 *
 * 0 A.D. is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 2 of the License, or
 * (at your option) any later version.
 *
 * 0 A.D. is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with 0 A.D.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "js/jsapi.h"

// Exception-safety and GC-safety wrapper for JSIdArray
// (TODO: it'd be nicer to use the existing js::AutoIdArray but currently that
// requires a load of semi-private SpiderMonkey headers that cause a load of compile warnings)
class IdArrayWrapper
{
	JSContext* m_cx;
	JSIdArray* m_ida;
public:
	IdArrayWrapper(JSContext* cx, JSIdArray* ida) :
		m_cx(cx), m_ida(ida)
	{
		for (jsint i = 0; i < m_ida->length; ++i)
			if (!JS_AddRoot(m_cx, &m_ida->vector[i]))
				debug_warn(L"JS_AddRoot failed");
	}
	~IdArrayWrapper()
	{
		for (jsint i = 0; i < m_ida->length; ++i)
			if (!JS_RemoveRoot(m_cx, &m_ida->vector[i]))
				debug_warn(L"JS_RemoveRoot failed");
		JS_DestroyIdArray(m_cx, m_ida);
	}
};

