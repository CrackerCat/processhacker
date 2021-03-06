﻿/*
 * Process Hacker - 
 *   ITreeModel implementation for the process tree
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

// The event 'event' is never used
#pragma warning disable 0067

using System;
using System.Collections.Generic;
using System.Windows.Forms;
using Aga.Controls.Tree;
using ProcessHacker.Common;
using ProcessHacker.Native;

namespace ProcessHacker
{
    /// <summary>
    /// The process tree model. None of the methods are thread-safe.
    /// </summary>
    public class ProcessTreeModel : ITreeModel
    {
        private readonly ProcessTree _tree;
        private readonly Dictionary<int, ProcessNode> _processes = new Dictionary<int, ProcessNode>();
        private readonly List<ProcessNode> _roots = new List<ProcessNode>();

        public ProcessTreeModel(ProcessTree tree)
        {
            _tree = tree;
        }

        public void Add(ProcessItem item)
        {
            ProcessNode itemNode = new ProcessNode(item);

            // Add the process to the list of all processes.
            _processes.Add(item.Pid, itemNode);

            // Find the process' parent and add the process to it if we found it.
            if (item.HasParent && _processes.ContainsKey(item.ParentPid))
            {
                ProcessNode parent = _processes[item.ParentPid];

                parent.Children.Add(itemNode);
                itemNode.Parent = parent;
            }
            else
            {
                // The process doesn't have a parent, so add it to the root nodes.
                _roots.Add(itemNode);
            }

            itemNode.RefreshTreePath();

            // Find this process' children and fix them up.

            // We need to create a copy of the array because we may need 
            // to modify the roots list.
            ProcessNode[] roots = _roots.ToArray();

            foreach (ProcessNode node in roots)
            {
                // Notice that we don't replace a node's parent if it 
                // already has one. This is to break potential cyclic 
                // references.
                if (node.Parent == null && node.ProcessItem.HasParent && node.PPid == item.Pid)
                {
                    // Remove the node from the root list and add it to our 
                    // process' child list.
                    _roots.Remove(node);
                    itemNode.Children.Add(node);
                    node.Parent = itemNode;
                    node.RefreshTreePathRecursive();
                }
            }

            this.StructureChanged(this, new TreePathEventArgs(new TreePath()));
        }

        public void Modify(ProcessItem oldItem, ProcessItem newItem)
        {
            ProcessNode node = _processes[newItem.Pid];

            node.ProcessItem = newItem;

            //if (node.ProcessItem.HasParent && node.PPID != -1)
            //    this.NodesChanged(this, new TreeModelEventArgs(this.GetPath(
            //        _processes.ContainsKey(node.PPID) ? _processes[node.PPID] : null),
            //        new object[] { node }));
        }

        public void Remove(ProcessItem item)
        {
            ProcessNode itemNode = _processes[item.Pid];
            ProcessNode[] itemChildren;

            // Dispose of the process node we're removing.
            itemNode.Dispose();

            itemChildren = itemNode.Children.ToArray();

            // Check if the node has a parent.
            if (itemNode.Parent == null)
            {
                if (_roots.Contains(itemNode))
                {
                    // Remove the process from the roots and make its children root nodes.
                    _roots.Remove(itemNode);
                    this.MoveChildrenToRoot(itemNode);
                }
            }
            else
            {
                if (itemNode.Parent.Children.Contains(itemNode))
                {
                    // Remove the node from its parent and make its children root nodes.
                    itemNode.Parent.Children.Remove(itemNode);
                    this.MoveChildrenToRoot(itemNode);
                }
            }

            // Remove the process from the process dictionary.
            _processes.Remove(item.Pid);
            this.StructureChanged(this, new TreePathEventArgs(new TreePath()));

            // Expand the children because TreeViewAdv collapses them by default.
            foreach (ProcessNode n in itemChildren)
            {
                try
                {
                    _tree.FindTreeNode(n).ExpandAll();
                }
                catch (Exception ex)
                {
                    Logging.Log(ex);
                }
            }


            _tree.Invalidate();
        }

        public TreePath GetPath(ProcessNode node)
        {
            if (node == null)
                return TreePath.Empty;

            if (this.GetSortColumn() != string.Empty)
            {
                return new TreePath(node);
            }
            
            return node.TreePath;
        }

        public void MoveChildrenToRoot(ProcessNode node)
        {
            ProcessNode[] children = node.Children.ToArray();

            foreach (ProcessNode child in children)
            {
                child.Parent = null;
                child.RefreshTreePathRecursive();
            }

            _roots.AddRange(children);
        }

        public Dictionary<int, ProcessNode> Nodes
        {
            get { return _processes; }
        }

        public ProcessNode[] Roots
        {
            get { return _roots.ToArray(); }
        }

        public string GetSortColumn()
        {
            foreach (TreeColumn column in _tree.Tree.Columns)
                if (column.SortOrder != SortOrder.None)
                    return column.Header.ToLowerInvariant();

            return string.Empty;
        }

        public SortOrder GetSortOrder()
        {
            foreach (TreeColumn column in _tree.Tree.Columns)
                if (column.SortOrder != SortOrder.None)
                    return column.SortOrder;

            return SortOrder.None;
        }

        public int ModifySort(int sortResult, SortOrder order)
        {
            switch (order)
            {
                case SortOrder.Ascending:
                    return -sortResult;
                case SortOrder.Descending:
                    return sortResult;
            }

            return 0;
        }

        public System.Collections.IEnumerable GetChildren(TreePath treePath)
        {
            if (this.GetSortColumn() != string.Empty)
            {
                List<ProcessNode> nodes = new List<ProcessNode>();
                string sortC = this.GetSortColumn();
                SortOrder sortO = this.GetSortOrder();

                nodes.AddRange(_processes.Values);

                nodes.Sort((n1, n2) =>
                {
                    // We have a problem here - the GdiHandlesNumber and UserHandlesNumber 
                    // properties are dynamically retrieved, so if n1 == n2 we may end up 
                    // getting different values for the same process due to the timing.
                    // If we do, then Array.Sort will throw an exception.
                    //
                    // The temporary HACK used here is to return 0 whenever n1 == n2.
                    if (n1 == n2)
                        return 0;

                    switch (sortC)
                    {
                        case "name":
                            return this.ModifySort(string.Compare(n1.Name, n2.Name), sortO);
                        case "pid":
                            return this.ModifySort(n1.Pid.CompareTo(n2.Pid), sortO);
                        case "pvt. memory":
                            return this.ModifySort(n1.ProcessItem.Process.VirtualMemoryCounters.PrivatePageCount.CompareTo(
                                n2.ProcessItem.Process.VirtualMemoryCounters.PrivatePageCount), sortO);
                        case "working set":
                            return this.ModifySort(n1.ProcessItem.Process.VirtualMemoryCounters.WorkingSetSize.CompareTo(
                                n2.ProcessItem.Process.VirtualMemoryCounters.WorkingSetSize), sortO);
                        case "peak working set":
                            return this.ModifySort(n1.ProcessItem.Process.VirtualMemoryCounters.PeakWorkingSetSize.CompareTo(
                                n2.ProcessItem.Process.VirtualMemoryCounters.PeakWorkingSetSize), sortO);
                        case "private ws":
                            return this.ModifySort(n1.PrivateWorkingSetNumber.CompareTo(n2.PrivateWorkingSetNumber), sortO);
                        case "shared ws":
                            return this.ModifySort(n1.SharedWorkingSetNumber.CompareTo(n2.SharedWorkingSetNumber), sortO);
                        case "shareable ws":
                            return this.ModifySort(n1.ShareableWorkingSetNumber.CompareTo(n2.ShareableWorkingSetNumber), sortO);
                        case "virtual size":
                            return this.ModifySort(n1.ProcessItem.Process.VirtualMemoryCounters.VirtualSize.CompareTo(
                                n2.ProcessItem.Process.VirtualMemoryCounters.VirtualSize), sortO);
                        case "peak virtual size":
                            return this.ModifySort(n1.ProcessItem.Process.VirtualMemoryCounters.PeakVirtualSize.CompareTo(
                                n2.ProcessItem.Process.VirtualMemoryCounters.PeakVirtualSize), sortO);
                        case "pagefile usage":
                            return this.ModifySort(n1.ProcessItem.Process.VirtualMemoryCounters.PagefileUsage.CompareTo(
                                n2.ProcessItem.Process.VirtualMemoryCounters.PagefileUsage), sortO);
                        case "peak pagefile usage":
                            return this.ModifySort(n1.ProcessItem.Process.VirtualMemoryCounters.PeakPagefileUsage.CompareTo(
                                n2.ProcessItem.Process.VirtualMemoryCounters.PeakPagefileUsage), sortO);
                        case "page faults":
                            return this.ModifySort(n1.ProcessItem.Process.VirtualMemoryCounters.PageFaultCount.CompareTo(
                                n2.ProcessItem.Process.VirtualMemoryCounters.PageFaultCount), sortO);
                        case "cpu":
                            return this.ModifySort(n1.ProcessItem.CpuUsage.CompareTo(n2.ProcessItem.CpuUsage), sortO);
                        case "username":
                            return this.ModifySort(string.Compare(n1.Username, n2.Username), sortO);
                        case "session id":
                            return this.ModifySort(n1.ProcessItem.SessionId.CompareTo(n2.ProcessItem.SessionId), sortO);
                        case "priority class":
                        case "base priority":
                            return this.ModifySort(n1.ProcessItem.Process.BasePriority.CompareTo(
                                n2.ProcessItem.Process.BasePriority), sortO);
                        case "description":
                            return this.ModifySort(string.Compare(n1.Description, n2.Description), sortO);
                        case "company":
                            return this.ModifySort(string.Compare(n1.Company, n2.Company), sortO);
                        case "file name":
                            return this.ModifySort(string.Compare(n1.FileName, n2.FileName), sortO);
                        case "command line":
                            return this.ModifySort(string.Compare(n1.CommandLine, n2.CommandLine), sortO);
                        case "threads":
                            return this.ModifySort(n1.ProcessItem.Process.NumberOfThreads.CompareTo(
                                n2.ProcessItem.Process.NumberOfThreads), sortO);
                        case "handles":
                            return this.ModifySort(n1.ProcessItem.Process.HandleCount.CompareTo(
                                n2.ProcessItem.Process.HandleCount), sortO);
                        case "gdi handles":
                            return this.ModifySort(n1.GdiHandlesNumber.CompareTo(n2.GdiHandlesNumber), sortO);
                        case "user handles":
                            return this.ModifySort(n1.UserHandlesNumber.CompareTo(n2.UserHandlesNumber), sortO);
                        case "i/o total":
                            return this.ModifySort(n1.IoTotalNumber.CompareTo(n2.IoTotalNumber), sortO);
                        case "i/o ro":
                            return this.ModifySort(n1.IoReadOtherNumber.CompareTo(n2.IoReadOtherNumber), sortO);
                        case "i/o w":
                            return this.ModifySort(n1.IoWriteNumber.CompareTo(n2.IoWriteNumber), sortO);
                        case "integrity":
                            return this.ModifySort(n1.IntegrityLevel.CompareTo(n2.IntegrityLevel), sortO);
                        case "i/o priority":
                            return this.ModifySort(n1.IoPriority.CompareTo(n2.IoPriority), sortO);
                        case "page priority":
                            return this.ModifySort(n1.PagePriority.CompareTo(n2.PagePriority), sortO);
                        case "start time":
                            return this.ModifySort(n1.ProcessItem.CreateTime.CompareTo(n2.ProcessItem.CreateTime), sortO);
                        case "start time (relative)":
                            // Invert the order - bigger dates are actually smaller if we use the relative time span.
                            return -this.ModifySort(n1.ProcessItem.CreateTime.CompareTo(n2.ProcessItem.CreateTime), sortO);
                        case "total cpu time":
                            return this.ModifySort((n1.ProcessItem.Process.KernelTime + n1.ProcessItem.Process.UserTime).
                                                       CompareTo(n2.ProcessItem.Process.KernelTime + n2.ProcessItem.Process.UserTime), sortO);
                        case "kernel cpu time":
                            return this.ModifySort(n1.ProcessItem.Process.KernelTime.CompareTo(
                                n2.ProcessItem.Process.KernelTime), sortO);
                        case "user cpu time":
                            return this.ModifySort(n1.ProcessItem.Process.UserTime.CompareTo(
                                n2.ProcessItem.Process.UserTime), sortO);
                        case "verification status":
                            return this.ModifySort(string.Compare(n1.VerificationStatus, n2.VerificationStatus), sortO);
                        case "verified signer":
                            return this.ModifySort(string.Compare(n1.VerifiedSigner, n2.VerifiedSigner), sortO);
                        default:
                            return 0;
                    }
                });

                return nodes;
            }

            if (treePath.IsEmpty())
                return _roots;
            
            return (treePath.LastNode as ProcessNode).Children;
        }

        public bool IsLeaf(TreePath treePath)
        {
            // When we're sorting the whole tree is a flat list, so there are no children.
            if (this.GetSortColumn() != string.Empty)
                return true;

            if (treePath.IsEmpty())
                return false;
            
            return (treePath.LastNode as ProcessNode).Children.Count == 0;
        }

        public event EventHandler<TreeModelEventArgs> NodesChanged;
        public event EventHandler<TreeModelEventArgs> NodesInserted;
        public event EventHandler<TreeModelEventArgs> NodesRemoved;
        public event EventHandler<TreePathEventArgs> StructureChanged;

        public void CallStructureChanged(TreePathEventArgs args)
        {
            this.StructureChanged(this, args);
        }
    }
}
