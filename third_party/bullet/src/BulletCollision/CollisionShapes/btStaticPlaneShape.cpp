/*
Bullet Continuous Collision Detection and Physics Library
Copyright (c) 2003-2009 Erwin Coumans  http://bulletphysics.org

This software is provided 'as-is', without any express or implied warranty.
In no event will the authors be held liable for any damages arising from the use of this software.
Permission is granted to anyone to use this software for any purpose, 
including commercial applications, and to alter it and redistribute it freely, 
subject to the following restrictions:

1. The origin of this software must not be misrepresented; you must not claim that you wrote the original software. If you use this software in a product, an acknowledgment in the product documentation would be appreciated but is not required.
2. Altered source versions must be plainly marked as such, and must not be misrepresented as being the original software.
3. This notice may not be removed or altered from any source distribution.
*/

// Altered for Rocket League build 261001 (rl-direct-clone roadmap 10.1): RL's bounded plane constructor and its broadphase box.

#include "btStaticPlaneShape.h"

#include "LinearMath/btTransformUtil.h"


btStaticPlaneShape::btStaticPlaneShape(const btVector3& planeNormal,btScalar planeConstant)
: btConcaveShape (), m_planeNormal(planeNormal.normalized()),
m_planeConstant(planeConstant),
m_localScaling(btScalar(0.),btScalar(0.),btScalar(0.)),
m_rlHalfExtentX(btScalar(BT_LARGE_FLOAT)),
m_rlHalfExtentY(btScalar(BT_LARGE_FLOAT))
{
	m_shapeType = STATIC_PLANE_PROXYTYPE;
	//	btAssert( btFuzzyZero(m_planeNormal.length() - btScalar(1.)) );
}

btStaticPlaneShape::btStaticPlaneShape(btScalar halfExtentX,btScalar halfExtentY)
: btConcaveShape (), m_planeNormal(btScalar(0.),btScalar(0.),btScalar(1.)),
m_planeConstant(btScalar(0.)),
m_localScaling(btScalar(0.),btScalar(0.),btScalar(0.)),
m_rlHalfExtentX(halfExtentX),
m_rlHalfExtentY(halfExtentY)
{
	m_shapeType = STATIC_PLANE_PROXYTYPE;
}


btStaticPlaneShape::~btStaticPlaneShape()
{
}



void btStaticPlaneShape::getAabb(const btTransform& t,btVector3& aabbMin,btVector3& aabbMax) const
{
	(void)t;
	/*
	btVector3 infvec (btScalar(BT_LARGE_FLOAT),btScalar(BT_LARGE_FLOAT),btScalar(BT_LARGE_FLOAT));

	btVector3 center = m_planeNormal*m_planeConstant;
	aabbMin = center + infvec*m_planeNormal;
	aabbMax = aabbMin;
	aabbMin.setMin(center - infvec*m_planeNormal);
	aabbMax.setMax(center - infvec*m_planeNormal); 
	*/

	aabbMin.setValue(btScalar(-BT_LARGE_FLOAT),btScalar(-BT_LARGE_FLOAT),btScalar(-BT_LARGE_FLOAT));
	aabbMax.setValue(btScalar(BT_LARGE_FLOAT),btScalar(BT_LARGE_FLOAT),btScalar(BT_LARGE_FLOAT));

}




// RL (1402a6730): the bounded plane enters the broadphase as a box, the stock plane as getAabb
void btStaticPlaneShape::rlGetAddAabb(const btTransform& t,btVector3& aabbMin,btVector3& aabbMax) const
{
	if (m_rlHalfExtentX < btScalar(BT_LARGE_FLOAT))
	{
		// the bounded rectangle's corners, then each swept BT_LARGE_FLOAT down the plane's z, summed in the asm's order
		const btMatrix3x3& b = t.getBasis();
		const btVector3& o = t.getOrigin();
		const btScalar hx = m_rlHalfExtentX, hy = m_rlHalfExtentY, zx = hx * btScalar(0.), zy = hy * btScalar(0.), nz = -btScalar(0.);
		btVector3 ex, ey, ez;
		for (int r = 0; r < 3; r++)
		{
			ex[r] = (b[r][0] * hx + b[r][1] * zx) + b[r][2] * zx;
			ey[r] = (b[r][0] * zy + b[r][1] * hy) + b[r][2] * zy;
			ez[r] = (b[r][0] * nz + b[r][1] * nz) - b[r][2] * btScalar(BT_LARGE_FLOAT);
		}
		const btVector3 c[4] = {(o - ex) - ey, (o + ex) + ey, (o - ex) + ey, (o + ex) - ey};
		aabbMin = aabbMax = c[0];
		for (int i = 1; i < 8; i++)
		{
			const btVector3 p = i < 4 ? c[i] : c[i - 4] + ez;
			aabbMin.setMin(p);
			aabbMax.setMax(p);
		}
		return;
	}
	getAabb(t,aabbMin,aabbMax);
}

void	btStaticPlaneShape::processAllTriangles(btTriangleCallback* callback,const btVector3& aabbMin,const btVector3& aabbMax) const
{

	btVector3 halfExtents = (aabbMax - aabbMin) * btScalar(0.5);
	btScalar radius = halfExtents.length();
	btVector3 center = (aabbMax + aabbMin) * btScalar(0.5);
	
	//this is where the triangles are generated, given AABB and plane equation (normal/constant)

	btVector3 tangentDir0,tangentDir1;

	//tangentDir0/tangentDir1 can be precalculated
	btPlaneSpace1(m_planeNormal,tangentDir0,tangentDir1);

	btVector3 supVertex0,supVertex1;

	btVector3 projectedCenter = center - (m_planeNormal.dot(center) - m_planeConstant)*m_planeNormal;
	
	btVector3 triangle[3];
	triangle[0] = projectedCenter + tangentDir0*radius + tangentDir1*radius;
	triangle[1] = projectedCenter + tangentDir0*radius - tangentDir1*radius;
	triangle[2] = projectedCenter - tangentDir0*radius - tangentDir1*radius;

	callback->processTriangle(triangle,0,0);

	triangle[0] = projectedCenter - tangentDir0*radius - tangentDir1*radius;
	triangle[1] = projectedCenter - tangentDir0*radius + tangentDir1*radius;
	triangle[2] = projectedCenter + tangentDir0*radius + tangentDir1*radius;

	callback->processTriangle(triangle,0,1);

}

void	btStaticPlaneShape::calculateLocalInertia(btScalar mass,btVector3& inertia) const
{
	(void)mass;

	//moving concave objects not supported
	
	inertia.setValue(btScalar(0.),btScalar(0.),btScalar(0.));
}

void	btStaticPlaneShape::setLocalScaling(const btVector3& scaling)
{
	m_localScaling = scaling;
}
const btVector3& btStaticPlaneShape::getLocalScaling() const
{
	return m_localScaling;
}
