/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/*
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 */

#ifndef INCLUDED_IXION_INFO_HPP
#define INCLUDED_IXION_INFO_HPP

#include "env.hpp"

namespace ixion {

IXION_DLLPUBLIC int get_version_major();
IXION_DLLPUBLIC int get_version_minor();
IXION_DLLPUBLIC int get_version_micro();

IXION_DLLPUBLIC int get_api_version_major();
IXION_DLLPUBLIC int get_api_version_minor();

}

#endif

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */
