﻿/*
 * Process Hacker - 
 *   service manager handle
 * 
 * Copyright (C) 2008 wj32
 * 
 * This file is part of Process Hacker.
 * 
 * Process Hacker is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * Process Hacker is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with Process Hacker.  If not, see <http://www.gnu.org/licenses/>.
 */

using ProcessHacker.Native.Api;
using ProcessHacker.Native.Security;

namespace ProcessHacker.Native.Objects
{
    /// <summary>
    /// Represents a handle to the Windows service manager.
    /// </summary>
    public class ServiceManagerHandle : ServiceBaseHandle<ScManagerAccess>
    {
        /// <summary>
        /// Connects to the Windows service manager.
        /// </summary>
        /// <param name="access">The desired access to the service manager.</param>
        public ServiceManagerHandle(ScManagerAccess desiredAccess)
        {
            this.Handle = Win32.OpenSCManager(0, 0, desiredAccess);

            if (this.Handle == 0)
                Win32.ThrowLastWin32Error();
        }

        public ServiceHandle CreateService(string name, string displayName,
            ServiceType type, string binaryPath)
        {
            return this.CreateService(name, displayName, type, ServiceStartType.DemandStart,
                ServiceErrorControl.Ignore, binaryPath, null, null, null);
        }

        public ServiceHandle CreateService(string name, string displayName,
            ServiceType type, ServiceStartType startType, string binaryPath)
        {
            return this.CreateService(name, displayName, type, startType,
                ServiceErrorControl.Ignore, binaryPath, null, null, null);
        }

        public ServiceHandle CreateService(string name, string displayName,
            ServiceType type, ServiceStartType startType, ServiceErrorControl errorControl,
            string binaryPath, string group, string accountName, string password)
        {
            int service;

            if ((service = Win32.CreateService(this, name, displayName, ServiceAccess.All,
                type, startType, errorControl, binaryPath, group, 0, 0, accountName, password)) == 0)
                Win32.ThrowLastWin32Error();

            return new ServiceHandle(service, true);
        }
    }
}
