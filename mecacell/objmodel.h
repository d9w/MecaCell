#ifndef MECACELL_OBJMODEL_H
#define MECACELL_OBJMODEL_H

#include "tools.h"
#include <vector>
#include <array>
#include <unordered_map>
#include <fstream>
#include <sstream>
#include <string>
#include <iostream>

using std::vector;
using std::string;
using std::unordered_map;
using std::array;

namespace MecaCell {

struct UV {
	float_t u, v;
	UV(float_t U, float_t V) : u(U), v(V){};
};

struct Triangle {
	array<unsigned int, 3> indices;
	Triangle() {}
	Triangle(array<unsigned int, 3> i) : indices(i) {}
	Triangle(unsigned int I0, unsigned int I1, unsigned int I2) : indices{{I0, I1, I2}} {}
};

class ObjModel {
public:
	vector<Vec> vertices;
	vector<UV> uv;
	vector<Vec> normals;
	vector<unordered_map<string, Triangle>> faces;

	ObjModel(const string &filepath) {
		std::ifstream file(filepath);
		string line;
		while (std::getline(file, line)) {
			vector<string> vs = splitStr(line, ' ');
			if (vs.size() > 1) {
				if (vs[0] == "v" && vs.size() > 3) {
					vertices.push_back(Vec(stod(vs[1]), stod(vs[2]), stod(vs[3])));
				} else if (vs[0] == "vt" && vs.size() > 2) {
					uv.push_back(UV(stod(vs[1]), stod(vs[2])));
				} else if (vs[0] == "vn" && vs.size() > 3) {
					normals.push_back(Vec(stod(vs[1]), stod(vs[2]), stod(vs[3])));
				} else if (vs[0] == "f" && vs.size() == 4) {
					unordered_map<string, Triangle> tf;
					for (size_t i = 1; i < vs.size(); ++i) {
						vector<string> index = splitStr(vs[i], '/');
						if (index.size() == 3) {
							tf["v"].indices[i - 1] = stoi(index[0]) - 1;
							if (!index[1].empty()) tf["t"].indices[i - 1] = stoi(index[1]) - 1;
							tf["n"].indices[i - 1] = stoi(index[2]) - 1;
						}
					}
					faces.push_back(tf);
				}
			}
		}
		//std::cout << "Object loaded, vertices.size = " << vertices.size() << ", normals.size = " << normals.size()
							//<< ", faces.size = " << faces.size() << std::endl;
	}
};
}
#endif
