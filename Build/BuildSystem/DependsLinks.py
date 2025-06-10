# Copyright 2025, LiserverYang. All rights reserved.

class TGraph:
    """
    The graph of dependency links.
    """
    
    def __init__(self):
        self.Data = {}

    def AddLink(self, begin: int, end: int):
        """
        Add an edge (begin, end) to this graph.
        """
        
        if begin not in self.Data:
            self.Data[begin] = []
        self.Data[begin].append(end)

    def AddLinks(self, begin: int, end: list[int]):
        """
        Add multiple edges (begin, end) to this graph.
        """

        for _end in end:
            self.AddLink(begin, _end)

    def TopologicalSort(self):
        """
        Perform a topological sort of the graph.
        Returns a tuple (sorted_list, has_cycle)
        `sorted_list` is the topologically sorted list of nodes.
        `has_cycle` is a boolean indicating if there is a cycle in the graph.
        """

        visited = set()
        temp_marked = set()
        sorted_list = []
        has_cycle = False

        def visit(node):
            nonlocal has_cycle
            if node in temp_marked:
                has_cycle = True
                return
            if node not in visited:
                temp_marked.add(node)
                for neighbor in self.Data.get(node, []):
                    visit(neighbor)
                temp_marked.remove(node)
                visited.add(node)
                sorted_list.append(node)

        for node in self.Data:
            if node not in visited:
                visit(node)
        
        # Check for nodes with no outgoing edges
        all_nodes = set(self.Data.keys())
        for edges in self.Data.values():
            all_nodes.update(edges)
        for node in all_nodes:
            if node not in visited:
                sorted_list.append(node)
        
        return sorted_list, has_cycle
