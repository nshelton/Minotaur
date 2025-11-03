#pragma once

#include <limits>
#include <utility>
#include <functional>
#include "core/Vec2.h"

// Simple 2D KD-Tree supporting insert, remove-by-key, and nearest neighbor.
// Keys are user-provided unique ints associated with each point.
class KdTree2D
{
public:
	KdTree2D() : m_root(nullptr) {}

	~KdTree2D() { clear(); }

	void clear()
	{
		clearRec(m_root);
		m_root = nullptr;
	}

	void insert(const Vec2 &p, int key)
	{
		m_root = insertRec(m_root, p, key, 0);
	}

	bool remove(int key)
	{
		bool removed = false;
		m_root = removeRec(m_root, key, removed);
		return removed;
	}

	// Returns pair<key, dist2>; if tree empty, returns <-1, inf>
	std::pair<int, float> nearest(const Vec2 &q) const
	{
		int bestKey = -1;
		float bestD2 = std::numeric_limits<float>::infinity();
		nearestRec(m_root, q, bestKey, bestD2);
		return {bestKey, bestD2};
	}

private:
	struct Node
	{
		Vec2 pt;
		int key;
		int axis; // 0:x, 1:y
		Node *left{nullptr};
		Node *right{nullptr};
		Node(const Vec2 &p, int k, int a) : pt(p), key(k), axis(a) {}
	};

	Node *m_root;

	static inline float dist2(const Vec2 &a, const Vec2 &b)
	{
		const float dx = a.x - b.x;
		const float dy = a.y - b.y;
		return dx * dx + dy * dy;
	}

	void clearRec(Node *n)
	{
		if (!n) return;
		clearRec(n->left);
		clearRec(n->right);
		delete n;
	}

	Node *insertRec(Node *n, const Vec2 &p, int key, int axis)
	{
		if (!n) return new Node(p, key, axis);

		const bool goLeft = (axis == 0) ? (p.x < n->pt.x) : (p.y < n->pt.y);
		if (goLeft)
			n->left = insertRec(n->left, p, key, 1 - axis);
		else
			n->right = insertRec(n->right, p, key, 1 - axis);
		return n;
	}

	Node *findMin(Node *n, int axis, int depthAxis) const
	{
		if (!n) return nullptr;

		if (depthAxis == axis)
		{
			if (!n->left) return n;
			return findMin(n->left, axis, 1 - depthAxis);
		}

		Node *lmin = findMin(n->left, axis, 1 - depthAxis);
		Node *rmin = findMin(n->right, axis, 1 - depthAxis);
		Node *minNode = n;
		if (lmin && ((axis == 0 ? lmin->pt.x < minNode->pt.x : lmin->pt.y < minNode->pt.y))) minNode = lmin;
		if (rmin && ((axis == 0 ? rmin->pt.x < minNode->pt.x : rmin->pt.y < minNode->pt.y))) minNode = rmin;
		return minNode;
	}

	Node *removeRec(Node *n, int key, bool &removed)
	{
		if (!n) return nullptr;

		if (key == n->key)
		{
			removed = true;
			if (n->right)
			{
				Node *m = findMin(n->right, n->axis, 1 - n->axis);
				n->pt = m->pt;
				n->key = m->key;
				n->right = removeRec(n->right, m->key, removed /*ignored*/);
				return n;
			}
			else if (n->left)
			{
				Node *m = findMin(n->left, n->axis, 1 - n->axis);
				n->pt = m->pt;
				n->key = m->key;
				n->left = removeRec(n->left, m->key, removed /*ignored*/);
				return n;
			}
			else
			{
				delete n;
				return nullptr;
			}
		}
		else
		{
			// Because we do not store the key's point here, removal must search both sides.
			n->left = removeRec(n->left, key, removed);
			if (!removed) n->right = removeRec(n->right, key, removed);
			return n;
		}
	}

	void nearestRec(Node *n, const Vec2 &q, int &bestKey, float &bestD2) const
	{
		if (!n) return;

		const float d2 = dist2(q, n->pt);
		if (d2 < bestD2)
		{
			bestD2 = d2;
			bestKey = n->key;
		}

		Node *first = nullptr;
		Node *second = nullptr;
		float diff = 0.0f;
		if (n->axis == 0)
		{
			diff = q.x - n->pt.x;
			if (diff < 0.0f) { first = n->left; second = n->right; }
			else { first = n->right; second = n->left; }
		}
		else
		{
			diff = q.y - n->pt.y;
			if (diff < 0.0f) { first = n->left; second = n->right; }
			else { first = n->right; second = n->left; }
		}

		nearestRec(first, q, bestKey, bestD2);
		if (diff * diff < bestD2)
			nearestRec(second, q, bestKey, bestD2);
	}
};


