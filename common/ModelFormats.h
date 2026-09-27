// Writes small models in formats that Assimp reads: OBJ with its material
// library, Collada with a skin and a clip, text FBX in metres, in centimetres,
// and with Z up, STL, and PLY. The importer is tested with real files without
// the repository storing any.
//
// Every file is written as text. Each writer returns false if the file cannot
// be written.

#pragma once

#include "Png.h"

#include <cstdint>
#include <fstream>
#include <string>
#include <vector>

namespace examples
{

// A box of the given size centred on the origin, as OBJ, with a material
// library naming a red material and a texture: a 16x16 checker written beside
// it as PNG. textureName is what the material library calls the image.
inline bool WriteObjBox(const std::string& path, float width, float height, float depth,
                        const std::string& textureName)
{
    const std::string directory = path.substr(0, path.find_last_of("/\\") + 1);
    const std::string stem = path.substr(directory.size(), path.find_last_of('.') - directory.size());

    std::vector<uint8_t> checker(16 * 16 * 4);
    for (int y = 0; y < 16; ++y)
    {
        for (int x = 0; x < 16; ++x)
        {
            const bool light = ((x / 4) + (y / 4)) % 2 == 0;
            uint8_t* pixel = &checker[(static_cast<size_t>(y) * 16 + x) * 4];
            pixel[0] = light ? 230 : 40;
            pixel[1] = light ? 80 : 20;
            pixel[2] = light ? 60 : 20;
            pixel[3] = 255;
        }
    }
    if (!WritePng(directory + textureName, 16, 16, checker))
    {
        return false;
    }

    std::ofstream material(directory + stem + ".mtl");
    material << "newmtl brick\n"
                "Kd 1.0 0.2 0.1\n"
                "Ks 0.2 0.2 0.2\n"
                "Ns 50\n"
                "d 1.0\n"
                "map_Kd "
             << textureName << "\n";
    if (!material)
    {
        return false;
    }

    const float x = width * 0.5f, y = height * 0.5f, z = depth * 0.5f;
    std::ofstream out(path);
    out << "# a box, " << width << " by " << height << " by " << depth << "\n"
        << "mtllib " << stem << ".mtl\n";
    const float corners[8][3] = { { -x, -y, -z }, { x, -y, -z }, { x, y, -z }, { -x, y, -z },
                                  { -x, -y, z },  { x, -y, z },  { x, y, z },  { -x, y, z } };
    for (const auto& corner : corners)
    {
        out << "v " << corner[0] << " " << corner[1] << " " << corner[2] << "\n";
    }
    out << "vt 0 0\nvt 1 0\nvt 1 1\nvt 0 1\n"
           "usemtl brick\n";
    // Counter-clockwise seen from outside; OBJ counts from 1.
    const int faces[6][4] = { { 1, 4, 3, 2 }, { 5, 6, 7, 8 }, { 1, 2, 6, 5 },
                              { 4, 8, 7, 3 }, { 1, 5, 8, 4 }, { 2, 3, 7, 6 } };
    for (const auto& face : faces)
    {
        out << "f " << face[0] << "/1 " << face[1] << "/2 " << face[2] << "/3 " << face[3] << "/4\n";
    }
    return static_cast<bool>(out);
}

// A column two metres tall on two joints, joint0 at the base and joint1 a metre
// up, skinned so the lower ring follows joint0, the middle ring both, and the
// top ring joint1; and one clip, "bend", that turns joint1 a quarter turn
// about Z over one second.
inline bool WriteColladaBendingColumn(const std::string& path)
{
    std::ofstream out(path);
    out << R"(<?xml version="1.0" encoding="utf-8"?>
<COLLADA xmlns="http://www.collada.org/2005/11/COLLADASchema" version="1.4.1">
  <asset><unit name="meter" meter="1"/><up_axis>Y_UP</up_axis></asset>
  <library_geometries>
    <geometry id="column-mesh" name="column">
      <mesh>
        <source id="column-positions">
          <float_array id="column-positions-array" count="36">
            -0.2 0 -0.2  0.2 0 -0.2  0.2 0 0.2  -0.2 0 0.2
            -0.2 1 -0.2  0.2 1 -0.2  0.2 1 0.2  -0.2 1 0.2
            -0.2 2 -0.2  0.2 2 -0.2  0.2 2 0.2  -0.2 2 0.2
          </float_array>
          <technique_common>
            <accessor source="#column-positions-array" count="12" stride="3">
              <param name="X" type="float"/><param name="Y" type="float"/><param name="Z" type="float"/>
            </accessor>
          </technique_common>
        </source>
        <vertices id="column-vertices"><input semantic="POSITION" source="#column-positions"/></vertices>
        <triangles count="20">
          <input semantic="VERTEX" source="#column-vertices" offset="0"/>
          <p>
            0 5 1  0 4 5   1 6 2  1 5 6   2 7 3  2 6 7   3 4 0  3 7 4
            4 9 5  4 8 9   5 10 6  5 9 10   6 11 7  6 10 11   7 8 4  7 11 8
            0 1 2  0 2 3   8 10 9  8 11 10
          </p>
        </triangles>
      </mesh>
    </geometry>
  </library_geometries>
  <library_controllers>
    <controller id="column-skin" name="skin">
      <skin source="#column-mesh">
        <bind_shape_matrix>1 0 0 0 0 1 0 0 0 0 1 0 0 0 0 1</bind_shape_matrix>
        <source id="skin-joints">
          <Name_array id="skin-joints-array" count="2">joint0 joint1</Name_array>
          <technique_common>
            <accessor source="#skin-joints-array" count="2" stride="1"><param name="JOINT" type="name"/></accessor>
          </technique_common>
        </source>
        <source id="skin-binds">
          <float_array id="skin-binds-array" count="32">
            1 0 0 0  0 1 0 0  0 0 1 0  0 0 0 1
            1 0 0 0  0 1 0 -1  0 0 1 0  0 0 0 1
          </float_array>
          <technique_common>
            <accessor source="#skin-binds-array" count="2" stride="16"><param name="TRANSFORM" type="float4x4"/></accessor>
          </technique_common>
        </source>
        <source id="skin-weights">
          <float_array id="skin-weights-array" count="2">1 0.5</float_array>
          <technique_common>
            <accessor source="#skin-weights-array" count="2" stride="1"><param name="WEIGHT" type="float"/></accessor>
          </technique_common>
        </source>
        <joints>
          <input semantic="JOINT" source="#skin-joints"/>
          <input semantic="INV_BIND_MATRIX" source="#skin-binds"/>
        </joints>
        <vertex_weights count="12">
          <input semantic="JOINT" source="#skin-joints" offset="0"/>
          <input semantic="WEIGHT" source="#skin-weights" offset="1"/>
          <vcount>1 1 1 1 2 2 2 2 1 1 1 1</vcount>
          <v>
            0 0  0 0  0 0  0 0
            0 1 1 1  0 1 1 1  0 1 1 1  0 1 1 1
            1 0  1 0  1 0  1 0
          </v>
        </vertex_weights>
      </skin>
    </controller>
  </library_controllers>
  <library_animations>
    <animation id="bend" name="bend">
      <source id="bend-input">
        <float_array id="bend-input-array" count="2">0 1</float_array>
        <technique_common>
          <accessor source="#bend-input-array" count="2" stride="1"><param name="TIME" type="float"/></accessor>
        </technique_common>
      </source>
      <source id="bend-output">
        <float_array id="bend-output-array" count="32">
          1 0 0 0  0 1 0 1  0 0 1 0  0 0 0 1
          0 -1 0 0  1 0 0 1  0 0 1 0  0 0 0 1
        </float_array>
        <technique_common>
          <accessor source="#bend-output-array" count="2" stride="16"><param name="TRANSFORM" type="float4x4"/></accessor>
        </technique_common>
      </source>
      <source id="bend-interpolation">
        <Name_array id="bend-interpolation-array" count="2">LINEAR LINEAR</Name_array>
        <technique_common>
          <accessor source="#bend-interpolation-array" count="2" stride="1"><param name="INTERPOLATION" type="name"/></accessor>
        </technique_common>
      </source>
      <sampler id="bend-sampler">
        <input semantic="INPUT" source="#bend-input"/>
        <input semantic="OUTPUT" source="#bend-output"/>
        <input semantic="INTERPOLATION" source="#bend-interpolation"/>
      </sampler>
      <channel source="#bend-sampler" target="joint1/transform"/>
    </animation>
  </library_animations>
  <library_visual_scenes>
    <visual_scene id="scene" name="scene">
      <node id="rig" name="rig" type="NODE">
        <node id="joint0" name="joint0" sid="joint0" type="JOINT">
          <matrix sid="transform">1 0 0 0 0 1 0 0 0 0 1 0 0 0 0 1</matrix>
          <node id="joint1" name="joint1" sid="joint1" type="JOINT">
            <matrix sid="transform">1 0 0 0 0 1 0 1 0 0 1 0 0 0 0 1</matrix>
          </node>
        </node>
      </node>
      <node id="column" name="column" type="NODE">
        <instance_controller url="#column-skin"><skeleton>#joint0</skeleton></instance_controller>
      </node>
    </visual_scene>
  </library_visual_scenes>
  <scene><instance_visual_scene url="#scene"/></scene>
</COLLADA>
)";
    return static_cast<bool>(out);
}

// A box as FBX, in the text form FBX also has. size is in the file's own units;
// unitScaleFactor says how many centimetres one of them is (1 for centimetres,
// 100 for metres). zUp writes it the way a Z-up tool does (Blender without
// its axis conversion), with the box's third size along Z, pointing up.
inline bool WriteFbxBox(const std::string& path, float sizeX, float sizeY, float sizeZ, double unitScaleFactor,
                        bool zUp)
{
    const float x = sizeX * 0.5f, y = sizeY * 0.5f, z = sizeZ * 0.5f;
    std::ofstream out(path);
    out << "; FBX 7.4.0 project file\n"
           "FBXHeaderExtension:  {\n"
           "\tFBXHeaderVersion: 1003\n"
           "\tFBXVersion: 7400\n"
           "\tCreator: \"ludifex examples\"\n"
           "}\n"
           "GlobalSettings:  {\n"
           "\tVersion: 1000\n"
           "\tProperties70:  {\n"
        << "\t\tP: \"UpAxis\", \"int\", \"Integer\", \"\"," << (zUp ? 2 : 1) << "\n"
        << "\t\tP: \"UpAxisSign\", \"int\", \"Integer\", \"\",1\n"
        << "\t\tP: \"FrontAxis\", \"int\", \"Integer\", \"\"," << (zUp ? 1 : 2) << "\n"
        << "\t\tP: \"FrontAxisSign\", \"int\", \"Integer\", \"\"," << (zUp ? -1 : 1) << "\n"
        << "\t\tP: \"CoordAxis\", \"int\", \"Integer\", \"\",0\n"
        << "\t\tP: \"CoordAxisSign\", \"int\", \"Integer\", \"\",1\n"
        << "\t\tP: \"UnitScaleFactor\", \"double\", \"Number\", \"\"," << unitScaleFactor << "\n"
        << "\t\tP: \"OriginalUnitScaleFactor\", \"double\", \"Number\", \"\"," << unitScaleFactor << "\n"
        << "\t}\n"
           "}\n"
           "Objects:  {\n"
           "\tGeometry: 1000, \"Geometry::Box\", \"Mesh\" {\n"
           "\t\tVertices: *24 {\n"
           "\t\t\ta: "
        << -x << "," << -y << "," << -z << "," << x << "," << -y << "," << -z << "," << x << "," << y << "," << -z
        << "," << -x << "," << y << "," << -z << "," << -x << "," << -y << "," << z << "," << x << "," << -y << ","
        << z << "," << x << "," << y << "," << z << "," << -x << "," << y << "," << z << "\n"
        << "\t\t}\n"
           "\t\tPolygonVertexIndex: *24 {\n"
           "\t\t\ta: 0,3,2,-2,4,5,6,-8,0,1,5,-5,3,7,6,-3,0,4,7,-4,1,2,6,-6\n"
           "\t\t}\n"
           "\t\tGeometryVersion: 124\n"
           "\t\tLayerElementMaterial: 0 {\n"
           "\t\t\tVersion: 101\n"
           "\t\t\tName: \"\"\n"
           "\t\t\tMappingInformationType: \"AllSame\"\n"
           "\t\t\tReferenceInformationType: \"IndexToDirect\"\n"
           "\t\t\tMaterials: *1 {\n"
           "\t\t\t\ta: 0\n"
           "\t\t\t}\n"
           "\t\t}\n"
           "\t\tLayer: 0 {\n"
           "\t\t\tVersion: 100\n"
           "\t\t\tLayerElement:  {\n"
           "\t\t\t\tType: \"LayerElementMaterial\"\n"
           "\t\t\t\tTypedIndex: 0\n"
           "\t\t\t}\n"
           "\t\t}\n"
           "\t}\n"
           "\tModel: 2000, \"Model::Box\", \"Mesh\" {\n"
           "\t\tVersion: 232\n"
           "\t\tProperties70:  {\n"
           "\t\t\tP: \"Lcl Translation\", \"Lcl Translation\", \"\", \"A\",0,0,0\n"
           "\t\t}\n"
           "\t\tShading: T\n"
           "\t\tCulling: \"CullingOff\"\n"
           "\t}\n"
           "\tMaterial: 3000, \"Material::Blue\", \"\" {\n"
           "\t\tVersion: 102\n"
           "\t\tShadingModel: \"phong\"\n"
           "\t\tMultiLayer: 0\n"
           "\t\tProperties70:  {\n"
           "\t\t\tP: \"DiffuseColor\", \"Color\", \"\", \"A\",0.1,0.3,0.9\n"
           "\t\t}\n"
           "\t}\n"
           "}\n"
           "Connections:  {\n"
           "\tC: \"OO\",2000,0\n"
           "\tC: \"OO\",1000,2000\n"
           "\tC: \"OO\",3000,2000\n"
           "}\n";
    return static_cast<bool>(out);
}

// A tetrahedron a metre across, as ASCII STL.
inline bool WriteStlTetrahedron(const std::string& path)
{
    std::ofstream out(path);
    const float p[4][3] = { { 0, 0, 0 }, { 1, 0, 0 }, { 0, 1, 0 }, { 0, 0, 1 } };
    const int faces[4][3] = { { 0, 2, 1 }, { 0, 1, 3 }, { 0, 3, 2 }, { 1, 2, 3 } };
    out << "solid tetrahedron\n";
    for (const auto& face : faces)
    {
        out << "  facet normal 0 0 0\n    outer loop\n";
        for (int corner : face)
        {
            out << "      vertex " << p[corner][0] << " " << p[corner][1] << " " << p[corner][2] << "\n";
        }
        out << "    endloop\n  endfacet\n";
    }
    out << "endsolid tetrahedron\n";
    return static_cast<bool>(out);
}

// A unit square as two triangles, as ASCII PLY with a colour at each corner.
inline bool WritePlySquare(const std::string& path)
{
    std::ofstream out(path);
    out << "ply\n"
           "format ascii 1.0\n"
           "element vertex 4\n"
           "property float x\nproperty float y\nproperty float z\n"
           "property uchar red\nproperty uchar green\nproperty uchar blue\n"
           "element face 2\n"
           "property list uchar int vertex_indices\n"
           "end_header\n"
           "0 0 0 255 0 0\n"
           "1 0 0 0 255 0\n"
           "1 1 0 0 0 255\n"
           "0 1 0 255 255 0\n"
           "3 0 1 2\n"
           "3 0 2 3\n";
    return static_cast<bool>(out);
}

} // namespace examples
