#ifndef CELLGROUP_HPP
#define CELLGROUP_HPP
#include "../primitives/sphere.hpp"
#include "../utilities/viewtools.h"

namespace MecacellViewer {
template <typename R> class CellGroup : public PaintStep<R> {
	using C = typename R::Cell;
	QOpenGLShaderProgram shader;
	unique_ptr<QOpenGLTexture> normalMap = nullptr;
	IcoSphere sphere;

 public:
	cellMode drawMode = plain;
	CellGroup() : PaintStep<R>("Cells"), sphere(4) {
		shader.addShaderFromSourceCode(QOpenGLShader::Vertex,
		                               shaderWithHeader(":/shaders/cell.vert"));
		shader.addShaderFromSourceCode(QOpenGLShader::Fragment,
		                               shaderWithHeader(":/shaders/cell.frag"));
		shader.link();
		normalMap = unique_ptr<QOpenGLTexture>(
		    new QOpenGLTexture(QImage(":/textures/cellNormalMap.jpg").mirrored()));
		normalMap->setMinificationFilter(QOpenGLTexture::LinearMipMapLinear);
		normalMap->setMagnificationFilter(QOpenGLTexture::Linear);
		sphere.load(shader);
	}

	void call(R *r, bool centersOnly = false, const ColorMode &colormode = color_normal) {
		const auto &cells = r->getScenario().getWorld().cells;
		if (cells.size() > 0) {
			const QMatrix4x4 view = r->getViewMatrix();
			const QMatrix4x4 projection = r->getProjectionMatrix();
			const auto viewV = r->getCamera().getViewVector();
			const auto camPos = r->getCamera().getPosition();
			const auto *selected = r->getSelectedCell();
			shader.bind();
			sphere.vao.bind();
			normalMap->bind(0);
			GL->glActiveTexture(GL_TEXTURE0);
			GL->glBindTexture(GL_TEXTURE_2D, normalMap->textureId());
			shader.setUniformValue(shader.uniformLocation("nmap"), 0);
			shader.setUniformValue(shader.uniformLocation("projection"), projection);
			shader.setUniformValue(shader.uniformLocation("view"), view);
			shader.setUniformValue(shader.uniformLocation("useUniformColor"), true);
			decltype((*cells.begin())->getPosition()) viewVec(viewV.x(), viewV.y(), viewV.z());
			decltype((*cells.begin())->getPosition()) camVec(camPos.x(), camPos.y(),
			                                                 camPos.z());
			for (auto &c : cells) {
				if (c->getVisible()) {
					QMatrix4x4 model;
					double radius = c->getBoundingBoxRadius();
					QVector3D center = toQV3D(c->getPosition());
					model.translate(center);
					const double r = c->getBoundingBoxRadius();
					if (centersOnly)
						model.scale(1, 1, 1);
					else
						model.scale(r, r, r);
					model.rotate(radToDeg(c->getOrientationRotation().teta),
					             toQV3D(c->getOrientationRotation().n));
					QMatrix4x4 nmatrix = (model).inverted().transposed();
					shader.setUniformValue(shader.uniformLocation("model"), model);
					shader.setUniformValue(shader.uniformLocation("normalMatrix"), nmatrix);
					auto color = cellColorToQVector(c, selected == c, colormode);
					shader.setUniformValue(shader.uniformLocation("color"), color);
					GL->glDrawElements(GL_TRIANGLES, sphere.indices.size(), GL_UNSIGNED_INT, 0);
				}
			}
			sphere.vao.release();
			shader.release();
		}
	}
};
}
#endif