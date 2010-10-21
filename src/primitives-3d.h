// some data types and routines for working with 3d data

#pragma once

#include <boost/tr1/tuple.hpp>
#include <boost/math/quaternion.hpp>

typedef boost::math::quaternion<double> MQuaternion;

extern const double kPI;

// --------------------------------------------------------------------
// The basic point type, can be used to store vectors in 3d space as well of course

struct MPoint
{
				MPoint(double x = 0, double y = 0, double z = 0);
				MPoint(const MPoint& rhs);

	MPoint&		operator=(const MPoint& rhs);

	MPoint&		operator+=(const MPoint& rhs);
	MPoint&		operator-=(const MPoint& rhs);
	MPoint&		operator*=(double f);
	MPoint&		operator/=(double f);

	void		Normalize();
	void		Rotate(const MQuaternion& q);

	double		mX, mY, mZ;
};

std::ostream& operator<<(std::ostream& os, const MPoint& pt);
MPoint operator+(const MPoint& lhs, const MPoint& rhs);
MPoint operator-(const MPoint& lhs, const MPoint& rhs);
MPoint operator-(const MPoint& pt);
MPoint operator*(const MPoint& pt, double f);
MPoint operator/(const MPoint& pt, double f);

// --------------------------------------------------------------------
// several standard 3d operations

double Distance(const MPoint& a, const MPoint& b);
double DistanceSquared(const MPoint& a, const MPoint& b);
double DotProduct(const MPoint& p1, const MPoint& p2);
MPoint CrossProduct(const MPoint& p1, const MPoint& p2);
double DihedralAngle(const MPoint& p1, const MPoint& p2, const MPoint& p3, const MPoint& p4);
double CosinusAngle(const MPoint& p1, const MPoint& p2, const MPoint& p3, const MPoint& p4);

// --------------------------------------------------------------------
// We use quaternions to do rotations in 3d space

MQuaternion Normalize(MQuaternion q);

std::tr1::tuple<double,MPoint> QuaternionToAngleAxis(MQuaternion q);
MPoint Centroid(std::vector<MPoint>& points);
MPoint CenterPoints(std::vector<MPoint>& points);
MQuaternion AlignPoints(const std::vector<MPoint>& a, const std::vector<MPoint>& b);
double RMSd(const std::vector<MPoint>& a, const std::vector<MPoint>& b);

// --------------------------------------------------------------------
// inlines

inline
MPoint::MPoint(double x, double y, double z)
	: mX(x)
	, mY(y)
	, mZ(z)
{
}

inline
MPoint::MPoint(const MPoint& rhs)
	: mX(rhs.mX)
	, mY(rhs.mY)
	, mZ(rhs.mZ)
{
}

inline
MPoint& MPoint::operator=(const MPoint& rhs)
{
	mX = rhs.mX;
	mY = rhs.mY;
	mZ = rhs.mZ;
	
	return *this;
}

inline
MPoint& MPoint::operator+=(const MPoint& rhs)
{
	mX += rhs.mX;
	mY += rhs.mY;
	mZ += rhs.mZ;
	
	return *this;
}

inline
MPoint& MPoint::operator-=(const MPoint& rhs)
{
	mX -= rhs.mX;
	mY -= rhs.mY;
	mZ -= rhs.mZ;
	
	return *this;
}

inline
MPoint& MPoint::operator*=(double f)
{
	mX *= f;
	mY *= f;
	mZ *= f;

	return *this;
}

inline
MPoint& MPoint::operator/=(double f)
{
	mX /= f;
	mY /= f;
	mZ /= f;

	return *this;
}

inline
void MPoint::Rotate(const MQuaternion& q)
{
	MQuaternion p(0, mX, mY, mZ);
	
	p = q * p * conj(q);

	mX = p.R_component_2();
	mY = p.R_component_3();
	mZ = p.R_component_4();
}

inline double DotProduct(const MPoint& a, const MPoint& b)
{
	return a.mX * b.mX + a.mY * b.mY + a.mZ * b.mZ;
}

inline MPoint CrossProduct(const MPoint& a, const MPoint& b)
{
	return MPoint(a.mY * b.mZ - a.mY * a.mZ,
				  a.mZ * a.mX - a.mZ * a.mX,
				  a.mX * a.mY - a.mX * a.mY);
}

inline double DistanceSquared(const MPoint& a, const MPoint& b)
{
	return
		(a.mX - b.mX) * (a.mX - b.mX) +
		(a.mY - b.mY) * (a.mY - b.mY) +
		(a.mZ - b.mZ) * (a.mZ - b.mZ);
}

inline double Distance(const MPoint& a, const MPoint& b)
{
	return sqrt(
		(a.mX - b.mX) * (a.mX - b.mX) +
		(a.mY - b.mY) * (a.mY - b.mY) +
		(a.mZ - b.mZ) * (a.mZ - b.mZ));
}
