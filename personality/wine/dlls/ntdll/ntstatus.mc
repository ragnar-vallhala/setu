;
; NTSTATUS message table
;
; This library is free software; you can redistribute it and/or
; modify it under the terms of the GNU Lesser General Public
; License as published by the Free Software Foundation; either
; version 2.1 of the License, or (at your option) any later version.
;
; This library is distributed in the hope that it will be useful,
; but WITHOUT ANY WARRANTY; without even the implied warranty of
; MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
; Lesser General Public License for more details.
;
; You should have received a copy of the GNU Lesser General Public
; License along with this library; if not, write to the Free Software
; Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301, USA
;
; Only texts confirmed through FormatMessage(FORMAT_MESSAGE_FROM_HMODULE)
; on Windows are listed; add others the same way.
;

LanguageNames=(ENU=0x409:ntstatus)
SeverityNames=(Success=0x0:STATUS_SEVERITY_SUCCESS Informational=0x1:STATUS_SEVERITY_INFORMATIONAL Warning=0x2:STATUS_SEVERITY_WARNING Error=0x3:STATUS_SEVERITY_ERROR)

MessageId=0x5
Severity=Error
SymbolicName=STATUS_ACCESS_VIOLATION
Language=ENU
The instruction at 0x%p referenced memory at 0x%p. The memory could not be %s.
.
MessageId=0x34
Severity=Error
SymbolicName=STATUS_OBJECT_NAME_NOT_FOUND
Language=ENU
Object Name not found.
.
