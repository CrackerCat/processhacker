﻿/*
 * Process Hacker - 
 *   memory region list
 * 
 * Copyright (C) 2008-2009 wj32
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

using System;
using System.Collections.Generic;
using System.Windows.Forms;
using ProcessHacker.Common;
using ProcessHacker.Common.Ui;
using ProcessHacker.Native;
using ProcessHacker.Native.Api;
using ProcessHacker.Native.Objects;
using ProcessHacker.Native.Security;
using ProcessHacker.UI;

namespace ProcessHacker.Components
{
    public partial class MemoryList : UserControl
    {
        private int _runCount;
        private MemoryProvider _provider;
        private bool _needsSort;
        private readonly List<ListViewItem> _needsAdd = new List<ListViewItem>();
        private readonly HighlightingContext _highlightingContext;
        public new event KeyEventHandler KeyDown;
        public new event MouseEventHandler MouseDown;
        public new event MouseEventHandler MouseUp;
        private int _pid;

        public MemoryList()
        {
            InitializeComponent();

            _highlightingContext = new HighlightingContext(listMemory);
            listMemory.KeyDown += this.listMemory_KeyDown;
            listMemory.MouseDown += this.listMemory_MouseDown;
            listMemory.MouseUp += this.listMemory_MouseUp;

            ColumnSettings.LoadSettings(Settings.Instance.MemoryListViewColumns, listMemory);
            listMemory.ContextMenu = menuMemory;
            GenericViewMenu.AddMenuItems(copyMemoryMenuItem.MenuItems, listMemory, null);

            listMemory.ListViewItemSorter = new SortedListViewComparer(listMemory)
            {
                SortColumn = 1,
                SortOrder = SortOrder.Ascending
            };

            (listMemory.ListViewItemSorter as SortedListViewComparer).CustomSorters.Add(2, (x, y) =>
            {
                MemoryItem ix = (MemoryItem)x.Tag;
                MemoryItem iy = (MemoryItem)y.Tag;

                return ix.Size.CompareTo(iy.Size);
            });
        }

        private void listMemory_MouseUp(object sender, MouseEventArgs e)
        {
            if (this.MouseUp != null)
                this.MouseUp(sender, e);
        }

        private void listMemory_MouseDown(object sender, MouseEventArgs e)
        {
            if (this.MouseDown != null)
                this.MouseDown(sender, e);
        }

        private void listMemory_KeyDown(object sender, KeyEventArgs e)
        {
            if (this.KeyDown != null)
                this.KeyDown(sender, e);

            if (!e.Handled)
            {
                switch (e.KeyCode)
                {
                    case Keys.Enter:
                        this.readWriteMemoryMemoryMenuItem_Click(null, null);
                        break;
                }
            }
        }

        #region Properties

        public override bool Focused
        {
            get { return listMemory.Focused; }
        }

        public override ContextMenu ContextMenu
        {
            get { return listMemory.ContextMenu; }
            set { listMemory.ContextMenu = value; }
        }

        public override ContextMenuStrip ContextMenuStrip
        {
            get { return listMemory.ContextMenuStrip; }
            set { listMemory.ContextMenuStrip = value; }
        }

        public ExtendedListView List
        {
            get { return listMemory; }
        }

        public MemoryProvider Provider
        {
            get { return _provider; }
            set
            {
                if (_provider != null)
                {
                    _provider.DictionaryAdded -= this.provider_DictionaryAdded;
                    _provider.DictionaryModified -= this.provider_DictionaryModified;
                    _provider.DictionaryRemoved -= this.provider_DictionaryRemoved;
                    _provider.Updated -= this.provider_Updated;
                }

                _provider = value;

                listMemory.Items.Clear();
                _pid = -1;

                if (_provider != null)
                {
                    _provider.DictionaryAdded += this.provider_DictionaryAdded;
                    _provider.DictionaryModified += this.provider_DictionaryModified;
                    _provider.DictionaryRemoved += this.provider_DictionaryRemoved;
                    _provider.Updated += this.provider_Updated;
                    _pid = _provider.Pid;

                    foreach (MemoryItem item in _provider.Dictionary.Values)
                    {
                        provider_DictionaryAdded(item);
                    }
                }
            }
        }

        #endregion

        #region Interfacing

        public void BeginUpdate()
        {
            listMemory.BeginUpdate();
        }

        public void EndUpdate()
        {
            listMemory.EndUpdate();
        }

        public ListView.ListViewItemCollection Items
        {
            get { return listMemory.Items; }
        }

        public ListView.SelectedListViewItemCollection SelectedItems
        {
            get { return listMemory.SelectedItems; }
        }

        #endregion

        private string GetProtectStr(MemoryProtection protect)
        {
            string protectStr;

            if (protect == MemoryProtection.AccessDenied)
                protectStr = string.Empty;
            else if ((protect & MemoryProtection.Execute) != 0)
                protectStr = "X";
            else if ((protect & MemoryProtection.ExecuteRead) != 0)
                protectStr = "RX";
            else if ((protect & MemoryProtection.ExecuteReadWrite) != 0)
                protectStr = "RWX";
            else if ((protect & MemoryProtection.ExecuteWriteCopy) != 0)
                protectStr = "WCX";
            else if ((protect & MemoryProtection.NoAccess) != 0)
                protectStr = "NA";
            else if ((protect & MemoryProtection.ReadOnly) != 0)
                protectStr = "R";
            else if ((protect & MemoryProtection.ReadWrite) != 0)
                protectStr = "RW";
            else if ((protect & MemoryProtection.WriteCopy) != 0)
                protectStr = "WC";
            else
                protectStr = "?";

            if ((protect & MemoryProtection.Guard) != 0)
                protectStr += "+G";
            if ((protect & MemoryProtection.NoCache) != 0)
                protectStr += "+NC";
            if ((protect & MemoryProtection.WriteCombine) != 0)
                protectStr = "+WCM";

            return protectStr;
        }

        private string GetStateStr(MemoryState state)
        {
            switch (state)
            {
                case MemoryState.Commit:
                    return "Commit";
                case MemoryState.Free:
                    return "Free";
                case MemoryState.Reserve:
                    return "Reserve";
                case MemoryState.Reset:
                    return "Reset";
                default:
                    return "Unknown";
            }
        }

        private string GetTypeStr(MemoryType type)
        {
            switch (type)
            {
                case MemoryType.Image:
                    return "Image";
                case MemoryType.Mapped:
                    return "Mapped";
                case MemoryType.Private:
                    return "Private";
                default:
                    return "Unknown";
            }
        }

        private void provider_Updated()
        {
            lock (_needsAdd)
            {
                if (_needsAdd.Count > 0)
                {
                    this.BeginInvoke(new MethodInvoker(() =>
                    {
                        lock (_needsAdd)
                        {
                            listMemory.Items.AddRange(_needsAdd.ToArray());
                            _needsAdd.Clear();
                            _needsAdd.TrimExcess();
                        }
                    }));
                }
            }

            _highlightingContext.Tick();

            if (_needsSort)
            {
                this.BeginInvoke(new MethodInvoker(() =>
                {
                    if (_needsSort)
                    {
                        listMemory.Sort();
                        _needsSort = false;
                    }
                }));
            }

            _runCount++;
        }

        private void FillMemoryListViewItem(ListViewItem litem, MemoryItem item)
        {
            switch (item.State)
            {
                case MemoryState.Free:
                    litem.Text = "Free";
                    break;
                default:
                    if (item.Type == MemoryType.Image)
                    {
                        if (!string.IsNullOrEmpty(item.ModuleName))
                            litem.Text = item.ModuleName;
                        else
                            litem.Text = "Image";

                        litem.Text += " (" + this.GetStateStr(item.State) + ")";
                    }
                    else
                    {
                        litem.Text = this.GetTypeStr(item.Type);
                        litem.Text += " (" + this.GetStateStr(item.State) + ")";
                    }
                    break;
            }

            litem.SubItems[1].Text = Utils.FormatAddress(item.Address);
            litem.SubItems[2].Text = Utils.FormatSize(item.Size);
            litem.SubItems[3].Text = GetProtectStr(item.Protection);
            litem.Tag = item;
        }

        private void provider_DictionaryAdded(MemoryItem item)
        {
            this.BeginInvoke(new MethodInvoker(() =>
                {
                    HighlightedListViewItem litem = new HighlightedListViewItem(_highlightingContext, item.RunId > 0 && _runCount > 0)
                    {
                        Name = item.Address.ToString()
                    };

                    litem.SubItems.Add(new ListViewItem.ListViewSubItem(litem, string.Empty));
                    litem.SubItems.Add(new ListViewItem.ListViewSubItem(litem, string.Empty));
                    litem.SubItems.Add(new ListViewItem.ListViewSubItem(litem, string.Empty));

                    this.FillMemoryListViewItem(litem, item);

                    _needsAdd.Add(litem);
                }));
        }

        private void provider_DictionaryModified(MemoryItem oldItem, MemoryItem newItem)
        {
            this.BeginInvoke(new MethodInvoker(() =>
            {
                lock (listMemory)
                {
                    ListViewItem litem = listMemory.Items[newItem.Address.ToString()];

                    if (litem != null)
                        this.FillMemoryListViewItem(litem, newItem);
                }
            }));
        }

        private void provider_DictionaryRemoved(MemoryItem item)
        {
            this.BeginInvoke(new MethodInvoker(() =>
            {
                lock (listMemory)
                {
                    // FIXME
                    try
                    {
                        listMemory.Items.RemoveByKey(item.Address.ToString());
                    }
                    catch (Exception ex)
                    {
                        Logging.Log(ex);
                    }
                }
            }));
        }

        public void SaveSettings()
        {
            Settings.Instance.MemoryListViewColumns = ColumnSettings.SaveSettings(listMemory);
        }

        private void listMemory_DoubleClick(object sender, EventArgs e)
        {
            readWriteMemoryMemoryMenuItem_Click(sender, e);
        }

        private void menuMemory_Popup(object sender, EventArgs e)
        {
            if (listMemory.SelectedIndices.Count == 1)
            {
                menuMemory.EnableAll();

                MemoryItem item = (MemoryItem)listMemory.SelectedItems[0].Tag;

                if (item.State != MemoryState.Commit ||
                    item.Type != MemoryType.Private)
                {                                         
                    freeMenuItem.Enabled = false;
                    decommitMenuItem.Enabled = false;
                }
            }
            else
            {
                //menuMemory.DisableAll();

                dumpMemoryMenuItem.Enabled = true;
                readWriteAddressMemoryMenuItem.Enabled = true;

                if (listMemory.SelectedIndices.Count > 1)
                {
                    copyMemoryMenuItem.Enabled = true;
                }

                if (listMemory.VirtualListSize > 0)
                {
                    selectAllMemoryMenuItem.Enabled = true;
                }
                else
                {
                    selectAllMemoryMenuItem.Enabled = false;
                }
            }
        }

        private void changeMemoryProtectionMemoryMenuItem_Click(object sender, EventArgs e)
        {
            MemoryItem item = listMemory.SelectedItems[0].Tag as MemoryItem;

            using (VirtualProtectWindow w = new VirtualProtectWindow(_pid, item.Address, item.Size))
            {
                w.ShowDialog();
            }
        }

        private void readWriteMemoryMemoryMenuItem_Click(object sender, EventArgs e)
        {
            if (listMemory.SelectedIndices.Count != 1)
                return;

            MemoryItem item = listMemory.SelectedItems[0].Tag as MemoryItem;

            MemoryEditor.ReadWriteMemory(_pid, item.Address, (int)item.Size, false);
        }

        private void dumpMemoryMenuItem_Click(object sender, EventArgs e)
        {
            using (SaveFileDialog sfd = new SaveFileDialog
            {
                FileName = "Memory.bin",
                Filter = "Binary Files (*.bin)|*.bin|All Files (*.*)|*.*"
            })
            {
                if (sfd.ShowDialog() == DialogResult.OK)
                {
                    try
                    {
                        using (ProcessHandle phandle = new ProcessHandle(_pid, ProcessAccess.VmRead))
                        using (FileHandle fhandle = FileHandle.CreateWin32(sfd.FileName, FileAccess.GenericWrite, FileShareMode.Read))
                        {
                            foreach (ListViewItem litem in listMemory.SelectedItems)
                            {
                                MemoryItem item = (MemoryItem)litem.Tag;

                                using (MemoryAlloc alloc = new MemoryAlloc((int)item.Size))
                                {
                                    phandle.ReadMemory(item.Address, (IntPtr)alloc, (int)item.Size);
                                    fhandle.Write(alloc.Memory, (int)item.Size);
                                }
                            }
                        }
                    }
                    catch (Exception ex)
                    {
                        PhUtils.ShowException("Unable to dump the selected memory regions", ex);
                    }
                }
            }
        }

        private void readWriteAddressMemoryMenuItem_Click(object sender, EventArgs e)
        {
            PromptBox prompt = new PromptBox();

            if (prompt.ShowDialog() == DialogResult.OK)
            {
                IntPtr address = new IntPtr(-1);
                IntPtr regionAddress = IntPtr.Zero;
                long regionSize = 0;
                bool found = false;

                try
                {
                    address = ((long)BaseConverter.ToNumberParse(prompt.Value)).ToIntPtr();
                }
                catch
                {
                    PhUtils.ShowError("You have entered an invalid address.");

                    return;
                }

                List<MemoryItem> items = new List<MemoryItem>();

                foreach (MemoryItem item in _provider.Dictionary.Values)
                    items.Add(item);

                items.Sort((i1, i2) => i1.Address.CompareTo(i2.Address));

                int i = 0;

                foreach (MemoryItem item in items)
                {
                    MemoryItem regionItem = null;

                    if (item.Address.CompareTo(address) > 0)
                    {
                        if (i > 0)
                            regionItem = items[i - 1];
                    }
                    else if (item.Address.CompareTo(address) == 0)
                    {
                        regionItem = items[i];
                    }

                    if (regionItem != null && address.CompareTo(regionItem.Address) >= 0)
                    {
                        listMemory.Items[regionItem.Address.ToString()].Selected = true;
                        listMemory.Items[regionItem.Address.ToString()].EnsureVisible();
                        regionAddress = regionItem.Address;
                        regionSize = regionItem.Size;
                        found = true;

                        break;
                    }

                    i++;
                }

                if (!found)
                {
                    PhUtils.ShowError("Unable to find the memory address.");
                    return;
                }

                MemoryEditor.ReadWriteMemory(
                    _pid, 
                    regionAddress, 
                    (int)regionSize, 
                    false, 
                    f => f.Select(address.Decrement(regionAddress).ToInt64(), 1)
                    );
            }
        }

        private void selectAllMemoryMenuItem_Click(object sender, EventArgs e)
        {
            this.listMemory.Items.SelectAll();
        }

        private void freeMenuItem_Click(object sender, EventArgs e)
        {
            if (PhUtils.ShowConfirmMessage(
                "free",
                "the memory region",
                "Freeing memory regions may cause the process to crash.",
                true
                ))
            {
                try
                {
                    using (ProcessHandle phandle = new ProcessHandle(_pid, ProcessAccess.VmOperation))
                    {
                        MemoryItem item = listMemory.SelectedItems[0].Tag as MemoryItem;

                        phandle.FreeMemory(item.Address, (int)item.Size, false);
                    }
                }
                catch (Exception ex)
                {
                    PhUtils.ShowException("Unable to free the memory region", ex);
                }
            }
        }

        private void decommitMenuItem_Click(object sender, EventArgs e)
        {
            if (PhUtils.ShowConfirmMessage(
                "decommit",
                "the memory region",
                "Decommitting memory regions may cause the process to crash.",
                true
                ))
            {
                try
                {
                    using (ProcessHandle phandle = new ProcessHandle(_pid, ProcessAccess.VmOperation))
                    {
                        MemoryItem item = listMemory.SelectedItems[0].Tag as MemoryItem;

                        phandle.FreeMemory(item.Address, (int)item.Size, true);
                    }
                }
                catch (Exception ex)
                {
                    PhUtils.ShowException("Unable to decommit the memory region", ex);
                }
            }
        }
    }
}
