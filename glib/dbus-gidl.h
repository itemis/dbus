/* -*- mode: C; c-file-style: "gnu" -*- */
/* dbus-gidl.h data structure describing an interface, to be generated from IDL
 *             or something
 *
 * Copyright (C) 2003  Red Hat, Inc.
 *
 * Licensed under the Academic Free License version 1.2
 * 
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 * 
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 *
 */
#ifndef DBUS_GLIB_IDL_H
#define DBUS_GLIB_IDL_H

#include <dbus/dbus.h>
#include <glib.h>

G_BEGIN_DECLS

typedef struct InterfaceInfo InterfaceInfo;
typedef struct MethodInfo    MethodInfo;
typedef struct SignalInfo    SignalInfo;
typedef struct ArgInfo       ArgInfo;

typedef enum
{
  ARG_IN,
  ARG_OUT
} ArgDirection;

typedef enum
{
  METHOD_SYNC,
  METHOD_ASYNC,
  METHOD_CANCELLABLE
} MethodStyle;

InterfaceInfo* interface_info_new         (void);
void           interface_info_ref         (InterfaceInfo *info);
void           interface_info_unref       (InterfaceInfo *info);
GSList*        interface_info_get_methods (InterfaceInfo *info);
GSList*        interface_info_get_signals (InterfaceInfo *info);

MethodInfo*    method_info_new            (void);
void           method_info_ref            (MethodInfo    *info);
void           method_info_unref          (MethodInfo    *info);

const char*    method_info_get_name       (MethodInfo    *info);
GSList*        method_info_get_args       (MethodInfo    *info);
MethodStyle    method_info_get_style      (MethodInfo    *info);

SignalInfo*    signal_info_new            (void);
void           signal_info_ref            (SignalInfo    *info);
void           signal_info_unref          (SignalInfo    *info);

const char*    signal_info_get_name       (SignalInfo    *info);
GSList*        signal_info_get_args       (SignalInfo    *info);

ArgInfo*       arg_info_new               (void);
void           arg_info_ref               (ArgInfo       *info);
void           arg_info_unref             (ArgInfo       *info);
const char*    arg_info_get_name          (ArgInfo       *info);
int            arg_info_get_type          (ArgInfo       *info);
ArgDirection   arg_info_get_direction     (ArgInfo       *info);

G_END_DECLS

#endif /* DBUS_GLIB_IDL_H */
