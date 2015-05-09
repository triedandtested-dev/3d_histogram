import nuke

nodesmenu = nuke.menu("Nodes")
slyfoxmenu = nodesmenu.addMenu("SlyfoxFX","slyfoxfx.png")
slyfoxmenu.addCommand("Histogram(3D)", "nuke.createNode(\"sf_3DHisto\")")