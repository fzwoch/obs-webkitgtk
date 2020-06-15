#
# obs-webkitgtk. OBS Studio source plugin.
# Copyright (C) 2020 Florian Zwoch <fzwoch@gmail.com>
#
# This file is part of obs-webkitgtk.
#
# obs-webkitgtk is free software: you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation, either version 2 of the License, or
# (at your option) any later version.
#
# obs-webkitgtk is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with obs-webkitgtk. If not, see <http://www.gnu.org/licenses/>.
#

project('obs-webkitgtk', 'c')

shared_library('obs-webkitgtk',
    'obs-webkitgtk.c',
    name_prefix : '',
    dependencies : [
        dependency('libobs'),
        dependency('glib-2.0'),
    ],
    install : true,
    install_dir : join_paths('libdir', 'obs-plugins')
)

executable('obs-webkitgtk-helper',
    'obs-webkitgtk-helper.c',
    dependencies : [
        dependency('gtk+-3.0'),
        dependency('webkit2gtk-4.0'),
    ],
    install : true,
    install_dir : join_paths('libdir', 'obs-plugins')
)
