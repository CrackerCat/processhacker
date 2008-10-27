﻿/*
 * Process Hacker
 * 
 * Copyright (C) 2008 wj32
 * 
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

using System;
using System.Reflection;
using System.Windows.Forms;
using System.Drawing;

namespace ProcessHacker
{
    public partial class ProcessPrivileges : Form
    {
        private int _phandle;

        public ProcessPrivileges(int PID)
        {
            InitializeComponent();

            listPrivileges.ContextMenu = menuPrivileges;

            _phandle = Win32.OpenProcess(Win32.PROCESS_RIGHTS.PROCESS_QUERY_INFORMATION, 0, PID);

            if (_phandle == 0)
            {
                MessageBox.Show("Could not open process handle!", "Process Hacker", MessageBoxButtons.OK,
                    MessageBoxIcon.Error);

                this.Close();
            }

            typeof(ListView).GetProperty("DoubleBuffered",
                BindingFlags.NonPublic | BindingFlags.Instance).SetValue(listPrivileges, true, null);

            Win32.TOKEN_PRIVILEGES privileges = Win32.ReadTokenPrivileges(_phandle);

            if (privileges.PrivilegeCount > 0)
            {
                string name = Win32.GetPrivilegeName(privileges.Privileges[0].Luid);
                ListViewItem item = listPrivileges.Items.Add(name.ToLower(), name, 0);

                item.BackColor = GetAttributeColor(privileges.Privileges[0].Attributes);
                item.SubItems.Add(new ListViewItem.ListViewSubItem(item, 
                    GetAttributeString(privileges.Privileges[0].Attributes)));
                item.SubItems.Add(new ListViewItem.ListViewSubItem(item, Win32.GetPrivilegeDisplayName(name)));
            }

            for (int i = 0; i < privileges.PrivilegeCount - 1; i++)
            {
                string name = Win32.GetPrivilegeName(privileges.Privileges2[i].Luid);
                ListViewItem item = listPrivileges.Items.Add(name.ToLower(), name, 0);
                                                                                      
                item.BackColor = GetAttributeColor(privileges.Privileges2[i].Attributes);
                item.SubItems.Add(new ListViewItem.ListViewSubItem(item, 
                    GetAttributeString(privileges.Privileges2[i].Attributes)));
                item.SubItems.Add(new ListViewItem.ListViewSubItem(item, Win32.GetPrivilegeDisplayName(name)));
            }
        }

        private void ProcessPrivileges_Load(object sender, EventArgs e)
        {
            ListViewMenu.AddMenuItems(copyMenuItem.MenuItems, listPrivileges, null);

            this.Size = Properties.Settings.Default.PrivilegeWindowSize;
            ColumnSettings.LoadSettings(Properties.Settings.Default.PrivilegeListColumns, listPrivileges);
        }

        private void ProcessPrivileges_FormClosing(object sender, FormClosingEventArgs e)
        {
            Properties.Settings.Default.PrivilegeWindowSize = this.Size;
            Properties.Settings.Default.PrivilegeListColumns = ColumnSettings.SaveSettings(listPrivileges);
        }

        private string GetAttributeString(Win32.SE_PRIVILEGE_ATTRIBUTES Attributes)
        {
            if ((Attributes & Win32.SE_PRIVILEGE_ATTRIBUTES.SE_PRIVILEGE_ENABLED_BY_DEFAULT) != 0)
                return "Enabled By Default";
            else if ((Attributes & Win32.SE_PRIVILEGE_ATTRIBUTES.SE_PRIVILEGE_ENABLED) != 0)
                return "Enabled";
            else if (Attributes == Win32.SE_PRIVILEGE_ATTRIBUTES.SE_PRIVILEGE_DISABLED)
                return "Disabled";
            else
                return "Unknown";
        }

        private Color GetAttributeColor(Win32.SE_PRIVILEGE_ATTRIBUTES Attributes)
        {
            if ((Attributes & Win32.SE_PRIVILEGE_ATTRIBUTES.SE_PRIVILEGE_ENABLED_BY_DEFAULT) != 0)
                return Color.FromArgb(0xc0f0c0);
            else if ((Attributes & Win32.SE_PRIVILEGE_ATTRIBUTES.SE_PRIVILEGE_ENABLED) != 0)
                return Color.FromArgb(0xe0f0e0);
            else if (Attributes == Win32.SE_PRIVILEGE_ATTRIBUTES.SE_PRIVILEGE_DISABLED)
                return Color.FromArgb(0xf0e0e0);
            else
                return Color.White;
        }

        private void buttonClose_Click(object sender, EventArgs e)
        {
            this.Close();
        }

        private void menuPrivileges_Popup(object sender, EventArgs e)
        {
            if (listPrivileges.SelectedItems.Count == 0)
            {
                Misc.DisableAllMenuItems(menuPrivileges);
            }
            else
            {
                Misc.EnableAllMenuItems(menuPrivileges);
            }
        }

        private void enableMenuItem_Click(object sender, EventArgs e)
        {
            foreach (ListViewItem item in listPrivileges.SelectedItems)
            {
                if (Win32.WriteTokenPrivilege(_phandle, item.Text, Win32.SE_PRIVILEGE_ATTRIBUTES.SE_PRIVILEGE_ENABLED) == 0)
                {
                    if (MessageBox.Show("Could not enable " + item.Text + ":\n\n" + Win32.GetLastErrorMessage(), "Process Hacker",
                        MessageBoxButtons.OKCancel, MessageBoxIcon.Error) == DialogResult.Cancel)
                        return;
                }
                else
                {
                    if (item.SubItems[1].Text != "Enabled By Default")
                    {
                        item.BackColor = GetAttributeColor(Win32.SE_PRIVILEGE_ATTRIBUTES.SE_PRIVILEGE_ENABLED);
                        item.SubItems[1].Text = GetAttributeString(Win32.SE_PRIVILEGE_ATTRIBUTES.SE_PRIVILEGE_ENABLED);
                    }
                }
            }
        }

        private void disableMenuItem_Click(object sender, EventArgs e)
        {
            foreach (ListViewItem item in listPrivileges.SelectedItems)
            {
                if (item.SubItems[1].Text == "Enabled By Default")
                {
                    if (MessageBox.Show("Could not disable " + item.Text + ":\n\nInvalid operation.", "Process Hacker",
                        MessageBoxButtons.OKCancel, MessageBoxIcon.Error) == DialogResult.Cancel)
                        return;

                    continue;
                }

                if (Win32.WriteTokenPrivilege(_phandle, item.Text, Win32.SE_PRIVILEGE_ATTRIBUTES.SE_PRIVILEGE_DISABLED) == 0)
                {
                    if (MessageBox.Show("Could not disable " + item.Text + ":\n\n" + Win32.GetLastErrorMessage(), "Process Hacker",
                        MessageBoxButtons.OKCancel, MessageBoxIcon.Error) == DialogResult.Cancel)
                        return;
                }
                else
                {
                    item.BackColor = GetAttributeColor(Win32.SE_PRIVILEGE_ATTRIBUTES.SE_PRIVILEGE_DISABLED);
                    item.SubItems[1].Text = GetAttributeString(Win32.SE_PRIVILEGE_ATTRIBUTES.SE_PRIVILEGE_DISABLED);
                }
            }
        }

        private void removeMenuItem_Click(object sender, EventArgs e)
        {
            if (MessageBox.Show("Are you sure you want to remove the selected privilege(s)? This action is permanent.",
                "Process Hacker", MessageBoxButtons.YesNo, MessageBoxIcon.Exclamation) == DialogResult.Yes)
            {
                foreach (ListViewItem item in listPrivileges.SelectedItems)
                {
                    if (Win32.WriteTokenPrivilege(_phandle, item.Text, Win32.SE_PRIVILEGE_ATTRIBUTES.SE_PRIVILEGE_REMOVED) == 0)
                    {
                        if (MessageBox.Show("Could not remove " + item.Text + ":\n\n" + Win32.GetLastErrorMessage(), "Process Hacker",
                            MessageBoxButtons.OKCancel, MessageBoxIcon.Error) == DialogResult.Cancel)
                            return;
                    }
                    else
                    {
                        item.Remove();
                    }
                }
            }
        }

        private void selectAllMenuItem_Click(object sender, EventArgs e)
        {
            Misc.SelectAll(listPrivileges.Items);
        }
    }
}
