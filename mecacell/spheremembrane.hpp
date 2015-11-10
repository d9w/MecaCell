#ifndef SPHEREMEMBRANE_HPP
#define SPHEREMEMBRANE_HPP
#include "tools.h"
#include "connection.h"
#include "model.h"
#include "modelconnection.hpp"
#include "cellcellconnectionmanager.hpp"
#include <unordered_map>
#include <utility>
#include <vector>
#include <map>

// default breaking connection angle
#define DEFAULT_MAX_TETA M_PI / 12.0
// threshold (dot product) above which we want two model connections to be merged
#define MIN_MODEL_CONNECTION_SIMILARITY 0.8

#undef DBG
#define DBG DEBUG(spheremembrane)

namespace MecaCell {
template <typename Cell> class SphereMembrane {
	/********************** SphereMembrane class template **********************/
	// Abstract :
	// A sphere membrane is a very crude membrane approximation where a cell is
	// only defined by its radius. It is meant to be fast while allowing cells to
	// dynamically connect and bounce. It provides rudimentary (but fast)
	// volume conservation approximations and tries to maintain an adhesive
	// strength proportional to the contact surface.

 public:
	using CCCM = CellCellConnectionManager_vector<Cell>;
	friend CCCM;
	using ModelConnectionType = CellModelConnection<Cell>;
	using CellCellConnectionType = typename CCCM::ConnectionType;
	using CellCellConnectionContainer = typename CCCM::CellCellConnectionContainer;
	using CellModelConnectionContainer =
	    unordered_map<Model *,
	                  unordered_map<Cell *, vector<unique_ptr<CellModelConnection<Cell>>>>>;

 protected:
	Cell *cell;
	CCCM cccm;
	vector<ModelConnectionType *> modelConnections;
	float_t baseRadius = DEFAULT_CELL_RADIUS;
	float_t radius = DEFAULT_CELL_RADIUS;
	float_t correctedRadius =
	    DEFAULT_CELL_RADIUS;  // taking volume conservation into account
	float_t stiffness = DEFAULT_CELL_STIFFNESS;
	float_t dampRatio = DEFAULT_CELL_DAMP_RATIO;
	float_t angularStiffness = DEFAULT_CELL_ANG_STIFFNESS;
	float_t maxTeta = M_PI / 12.0;
	float_t pressure = 0;
	bool volumeConservation = true;

 public:
	SphereMembrane(Cell *c) : cell(c){};
	SphereMembrane(Cell *c, const SphereMembrane &sm)
	    : cell(c),
	      baseRadius(sm.baseRadius),
	      radius(sm.radius),
	      correctedRadius(sm.radius),
	      stiffness(sm.stiffness),
	      dampRatio(sm.dampRatio),
	      angularStiffness(sm.angularStiffness),
	      maxTeta(sm.maxTeta){};

	/**********************************************************
	                             GET
	***********************************************************/
	/************************ basics **************************/
	inline Cell *getCell() { return cell; }
	inline CCCM &getCellCellConnectionManager() { return cccm; }
	inline float_t getBoundingBoxRadius() const { return correctedRadius; };
	inline float_t getStiffness() const { return stiffness; }
	inline float_t getRadius() const { return radius; }
	inline float_t getBaseRadius() const { return baseRadius; }
	inline float_t getCorrectedRadius() const { return correctedRadius; }
	inline float_t getPressure() const { return pressure; }
	inline float_t getSqradius() const { return radius * radius; }
	inline float_t getDampRatio() const { return dampRatio; }
	inline float_t getAngularStiffness() const { return angularStiffness; }

	/************************ computed **************************/
	pair<vector<Cell *>, float_t> getConnectedCellAndMembraneDistance(const Vec &d) const {
		// /!\ assumes that d is normalized
		vector<Cell *> closestCells;
		float_t closestDist = correctedRadius;
		for (auto &cc : cccm.cellConnections) {
			auto &con = CCCM::getConnection(cc);
			con.updateLengthDirection();
			auto &otherCell = cell == con.getNode0() ? con.getNode1() : con.getNode0();
			Vec normal = cell == con.getNode0() ? -con.getDirection() : con.getDirection();
			float_t dot = normal.dot(d);
			if (dot < 0) {
				float_t midpoint =
				    con.getLength() * radius / (radius + otherCell->membrane.radius);
				float_t l = -midpoint / dot;
				if (fuzzyEqual(l, closestDist)) {
					closestCells.push_back(otherCell);
				} else if (l < closestDist) {
					closestDist = l;
					closestCells = {otherCell};
				}
			}
		}
		return {closestCells, closestDist};
	}

	inline vector<Cell *> getConnectedCell(const Vec &d) const {
		// /!\ multiple cells can be at the same shortest distance
		return get<0>(getConnectedCellAndMembraneDistance(d));
	}
	inline float_t getPreciseMembraneDistance(const Vec &d) const {
		return get<1>(getConnectedCellAndMembraneDistance(d));
	}

	inline float_t getVolume() const {
		return (4.0 / 3.0) * M_PI * radius * radius * radius;
	}
	inline float_t getBaseVolume() const {
		return (4.0 / 3.0) * M_PI * baseRadius * baseRadius * baseRadius;
	}
	inline float_t getMomentOfInertia() const { return 4.0 * cell->mass * radius * radius; }
	double getCurrentActualVolume() {
		double targetVol =
		    (4.0 / 3.0) * M_PI * correctedRadius * correctedRadius * correctedRadius;
		double volumeLoss = 0;
		for (auto &cc : cccm.cellConnections) {
			auto &con = CCCM::getConnection(cc);
			auto &other = cell == con.getNode0() ? con.getNode1() : con.getNode0();
			double midpoint = con.getLength() * radius / (radius + other->membrane.radius);
			double h = getCorrectedRadius() - midpoint;
			volumeLoss +=
			    (M_PI * h / 6.0) *
			    (3.0 * (getCorrectedRadius() * getCorrectedRadius() - midpoint * midpoint) +
			     h * h);
		}
		return targetVol - volumeLoss;
	}

	static inline float_t getConnectionLength(const Cell *c0, const Cell *c1) {
		return getConnectionLength(
		    c0->membrane.correctedRadius + c1->membrane.correctedRadius,
		    min(c0->getAdhesionWith(c1), c1->getAdhesionWith(c0)));
	}

	static float_t getConnectionLength(const float_t l, const float_t adh) {
		if (adh > ADH_THRESHOLD)
			return mix(MAX_CELL_ADH_LENGTH * l, MIN_CELL_ADH_LENGTH * l, adh);
		return l;
	}

	/**********************************************************
	                             SET
	***********************************************************/
	void setRadius(float_t r) {
		radius = r;
		correctedRadius = r;
	}
	void setBaseRadius(float_t r) { baseRadius = r; }
	void setRadiusRatio(float_t r) {
		radius = r * baseRadius;
		correctedRadius = r;
	}
	void setVolume(float_t v) { setRadius(cbrt(v / (4.0 * M_PI / 3.0))); }
	void setStiffness(float_t s) { stiffness = s; }
	void setAngularStiffness(float_t s) { angularStiffness = s; }

	/**********************************************************
	                           UPDATE
	***********************************************************/
	template <typename Integrator> void updatePositionsAndOrientations(double dt) {
		Integrator::updatePosition(*cell, dt);
		Integrator::updateOrientation(*cell, dt);
		if (volumeConservation) compensateVolumeLoss();
		cell->markAsNotTested();
	}

	void computePressure() {
		float_t surface = 4.0 * M_PI * radius * radius;
		pressure = roundN(cell->totalForce / surface);
	}

	double compensateVolumeLoss() {
		// just updates the correctedRadius
		double targetVol = getVolume();
		double volumeLoss = 0;
		for (auto &cc : cccm.cellConnections) {
			auto &con = CCCM::getConnection(cc);
			auto &other = cell == con.getNode0() ? con.getNode1() : con.getNode0();
			double midpoint = con.getLength() * radius / (radius + other->membrane.radius);
			double h = radius - midpoint;
			volumeLoss +=
			    (M_PI * h / 6.0) * (3.0 * (radius * radius - midpoint * midpoint) + h * h);
		}
		correctedRadius = roundN(cbrt((targetVol + 1.3 * volumeLoss) / ((4.0 / 3.0) * M_PI)));
	}

	void resetForces() {
		cell->force = Vec::zero();
		cell->torque = Vec::zero();
	}
	void updateStats() { computePressure(); }

	/**********************************************************
	                       CONNECTIONS
	***********************************************************/
	/******************** with 3D models **********************/
	void addModelConnection(ModelConnectionType *con) { modelConnections.push_back(con); }
	void removeModelConnection(ModelConnectionType *con) {
		modelConnections.erase(remove(modelConnections.begin(), modelConnections.end(), con),
		                       modelConnections.end());
	}

	template <typename SpacePartition>
	static void checkForCellModelCollisions(
	    vector<Cell *> &cells, unordered_map<string, Model>,
	    CellModelConnectionContainer &cellModelConnections, SpacePartition &modelGrid) {
		for (auto &m : cellModelConnections) {
			for (auto &c : m.second) {
				for (auto &conn : c.second) {
					conn->dirty = true;
				}
			}
		}
		for (auto &c : cells) {
			// for each cell, we find if a cell - model collision is possible.
			auto toTest = modelGrid.retrieve(c->getPosition(), c->getBoundingBoxRadius());
			for (const auto &mf : toTest) {
				// for each pair <model*, faceId> mf potentially colliding with c
				const Vec &p0 = mf.first->vertices[mf.first->faces[mf.second].indices[0]];
				const Vec &p1 = mf.first->vertices[mf.first->faces[mf.second].indices[1]];
				const Vec &p2 = mf.first->vertices[mf.first->faces[mf.second].indices[2]];
				// checking if cell c is in contact with triangle p0, p1, p2
				pair<bool, Vec> projec = projectionIntriangle(p0, p1, p2, c->getPosition());
				// projec = {projection inside triangle, projection coordinates}
				// TODO: we also need to check if the connection should be on a vertice

				Vec currentDirection = projec.second - c->getPosition();
				if (projec.first &&
				    currentDirection.sqlength() < pow(c->getBoundingBoxRadius(), 2)) {
					// we have a potential connection. Now we consider 2 cases:
					// 1 - brand new connection (easy)
					// 2 - older connection	(we need to update it)
					//  => same cell/model pair + similar bounce angle (same face or similar normal)
					currentDirection.normalize();
					bool alreadyExist = false;
					if (cellModelConnections.count(mf.first) &&
					    cellModelConnections[mf.first].count(c)) {
						for (auto &otherconn : cellModelConnections[mf.first][c]) {
							Vec prevDirection =
							    (otherconn->bounce.getNode0().getPosition() - c->getPrevposition())
							        .normalized();
							if (prevDirection.dot(currentDirection) > MIN_MODEL_CONNECTION_SIMILARITY) {
								alreadyExist = true;
								otherconn->dirty = false;
								// case n° 2, we want to update otherconn
								// first, the bounce spring
								otherconn->bounce.getNode0().position = projec.second;
								otherconn->bounce.getNode0().face = mf.second;
								// then the anchor. It's just another simple spring that is always at the
								// same height as the cell (orthogonal to the bounce spring)
								// it has a restlength of 0 and follows the cell when its length is more
								// than the cell's radius;
								if (otherconn->anchor.getSc().length > 0) {
									// first we keep the anchor at cell height
									const Vec &anchorDirection = otherconn->anchor.getSc().direction;
									Vec crossp =
									    currentDirection.cross(currentDirection.cross(anchorDirection));
									if (crossp.sqlength() > c->membrane.radius * 0.02) {
										crossp.normalize();
										float_t projLength = min(
										    (otherconn->anchor.getNode0().getPosition() - c->getPosition())
										        .dot(crossp),
										    c->membrane.radius);
										otherconn->anchor.getNode0().position =
										    c->getPosition() + projLength * crossp;
									}
								}
								break;
							}
						}
					}
					if (!alreadyExist) {
						// new connection
						float_t adh = c->getAdhesionWithModel(mf.first->name);
						float_t l = mix(MAX_CELL_ADH_LENGTH * c->membrane.correctedRadius,
						                MIN_CELL_ADH_LENGTH * c->membrane.correctedRadius, adh);
						unique_ptr<CellModelConnection<Cell>> cmc(new CellModelConnection<Cell>(
						    Connection<SpaceConnectionPoint, Cell *>(
						        {SpaceConnectionPoint(c->getPosition()), c},  // N0, N1
						        Spring(100, dampingFromRatio(0.9, c->getMass(), 100),
						               0)),  // anchor
						    Connection<ModelConnectionPoint, Cell *>(
						        {ModelConnectionPoint(mf.first, projec.second, mf.second),
						         c},  // N0, N1
						        Spring(c->membrane.getStiffness(),
						               dampingFromRatio(c->membrane.getDampRatio(), c->getMass(),
						                                c->membrane.getStiffness() * 1.0),
						               l)  // bounce
						        )));
						cmc->anchor.tjEnabled = false;
						c->membrane.addModelConnection(cmc.get());
						cellModelConnections[mf.first][c].push_back(move(cmc));
					}
				}
			}
		}
		// clean up: dirty connections
		for (auto &m : cellModelConnections) {
			for (auto &c : m.second) {
				for (auto it = c.second.begin(); it != c.second.end();) {
					if ((*it)->dirty) {
						c.first->membrane.removeModelConnection(it->get());
						it = c.second.erase(it);
					} else {
						++it;
					}
				}
			}
		}
		// clean up: empty entries
		for (auto itM = cellModelConnections.begin(); itM != cellModelConnections.end();) {
			if (itM->second.empty()) {
				itM = cellModelConnections.erase(itM);
			} else {
				for (auto itC = itM->second.begin(); itC != itM->second.end();) {
					if (itC->second.empty()) {
						itC = itM->second.erase(itC);
					} else {
						++itC;
					}
				}
				++itM;
			}
		}
	}

	/******************** between cells ***********************/
	template <typename SpacePartition>
	static void checkForCellCellConnections(
	    vector<Cell *> &cells, CellCellConnectionContainer &cellCellConnections,
	    SpacePartition &grid) {
		for (auto &cc : cellCellConnections) {
			CCCM::getConnection(cc).updateLengthDirection();
		}
		unordered_set<ordered_pair<Cell *>> newConnections;
		grid.clear();
		for (const auto &c : cells) grid.insert(c);
		auto gridCells = grid.getThreadSafeGrid();
		for (auto &batch : gridCells) {
			for (size_t i = 0; i < batch.size(); ++i) {
				for (size_t j = 0; j < batch[i].size(); ++j) {
					for (size_t k = j + 1; k < batch[i].size(); ++k) {
						stringstream header;
						auto op = make_ordered_cell_pair(batch[i][j], batch[i][k]);
						stringstream cotest;
						Vec AB = roundN(op.first->position - op.second->position);
						float_t sqDistance = AB.sqlength();
						float_t sqMaxLength = pow(
						    op.first->membrane.correctedRadius + op.second->membrane.correctedRadius,
						    2);
						if (sqDistance <= sqMaxLength) {
							if (!CCCM::areConnected(op.first, op.second) && !newConnections.count(op) &&
							    op.first != op.second) {
								float_t dist = sqrt(sqDistance);
								Vec dir = AB / dist;
								if (dist <
								    roundN(op.first->membrane.getConnectedCellAndMembraneDistance(dir)
								               .second) +
								        roundN(
								            op.second->membrane.getConnectedCellAndMembraneDistance(-dir)
								                .second)) {
									newConnections.insert(op);
								}
							}
						}
					}
				}
			}
		}
		for (auto &nc : newConnections) {
			createConnection(nc.first, nc.second, cellCellConnections);
		}
	}

	static void updateCellModelConnections(CellModelConnectionContainer &con, float_t dt) {
		for (auto &mod : con) {
			for (auto &vec : mod.second) {
				for (auto &c : vec.second) {
					c->computeForces(dt);
				}
			}
		}
	}

	inline float_t getConnectionMidpoint(const Cell *other,
	                                     const CellCellConnectionType *connection) {
		return connection->getLength() * radius / (radius + other->membrane.radius);
	}

	static void updateCellCellConnections(CellCellConnectionContainer &con, float_t dt) {
		// we update forces & delete impossible connections (cells not in contact anymore...)
		// here connections are just beams between cells centers

		vector<tuple<Cell *, Cell *, CellCellConnectionType *>> toErase;
		for (auto &cc : con) {
			auto &co = CCCM::getConnection(cc);
			co.updateLengthDirection();
		}
		for (auto &cc : con) {
			auto &connection = CCCM::getConnection(cc);
			auto &c0 = connection.getNode0();
			auto &c1 = connection.getNode1();
			// we check if the connection still makes sense
			auto t0 = c0->membrane.getConnectedCellAndMembraneDistance(
			    roundN(connection.getDirection()));
			auto t1 = c1->membrane.getConnectedCellAndMembraneDistance(
			    roundN(-connection.getDirection()));
			auto v0 = get<0>(t0);  // c0->membrane.getConnectedCell(connection.getDirection());
			auto v1 = get<0>(t1);  // c1->membrane.getConnectedCell(-connection.getDirection());
			auto c1ClosestToC0 = isInVector(c1, v0);
			auto c0ClosestToC1 = isInVector(c0, v1);
			if (connection.getSc().length > roundN(c0->getMembrane().correctedRadius +
			                                       c1->getMembrane().correctedRadius) ||
			    !c0ClosestToC1 || !c1ClosestToC0) {
				toErase.push_back(
				    tuple<Cell *, Cell *, CellCellConnectionType *>(c0, c1, &connection));
			} else {
				// connection still ok, we update its parameters
				// according to contact surface and cells volumes
				float_t contactSurface =
				    roundN(M_PI * (pow(connection.getSc().length, 2) +
				                   pow((c0->membrane.radius + c1->membrane.radius) / 2.0, 2)));
				connection.getFlex().first.setCurrentKCoef(contactSurface);
				connection.getFlex().second.setCurrentKCoef(contactSurface);
				connection.getTorsion().first.setCurrentKCoef(contactSurface);
				connection.getTorsion().second.setCurrentKCoef(contactSurface);
				float_t newl = getConnectionLength(
				    c0->membrane.correctedRadius + c1->membrane.correctedRadius,
				    (c0->getAdhesionWith(c1) + c1->getAdhesionWith(c0)) * 0.5);
				connection.getSc().setRestLength(roundN(newl));
				// then we compute the forces
				connection.computeForces(dt);
			}
		}
		for (auto &p : toErase) {
			CCCM::disconnect(con, get<0>(p), get<1>(p), get<2>(p));
		}
	}

	static inline void disconnectAndDeleteAllConnections(Cell *c0,
	                                                     CellCellConnectionContainer &con) {
		// TODO: handle model connections
		auto cop = c0->connectedCells;
		for (auto &c1 : cop) {
			CCCM::disconnect(con, c0, c1);
		}
	}

	static void createConnection(Cell *c0, Cell *c1, CellCellConnectionContainer &con) {
		float_t l = roundN(getConnectionLength(c0, c1));
		auto &membrane0 = c0->membrane;
		auto &membrane1 = c1->membrane;
		float_t vol0 = membrane0.getVolume();
		float_t vol1 = membrane1.getVolume();
		float_t volProportion = roundN(vol0 + vol1);
		float_t k =
		    roundN((membrane0.stiffness * vol0 + membrane1.stiffness * vol1) / volProportion);
		float_t dr =
		    roundN((membrane0.dampRatio * vol0 + membrane1.dampRatio * vol1) / volProportion);

		pair<Joint, Joint> joints = {
		    Joint(membrane0.getAngularStiffness(),
		          dampingFromRatio(dr, membrane0.getMomentOfInertia() * 2.0,
		                           membrane0.angularStiffness),
		          membrane0.maxTeta),
		    Joint(membrane1.getAngularStiffness(),
		          dampingFromRatio(dr, membrane1.getMomentOfInertia() * 2.0,
		                           membrane1.angularStiffness),
		          membrane1.maxTeta)};

		CCCM::createConnection(
		    con, c0, c1, make_pair(c0, c1),
		    Spring(k, roundN(dampingFromRatio(dr, c0->mass + c1->mass, k)), l), joints,
		    joints);
	}

	void division() { setRadius(getBaseRadius()); }
};
}

#endif
