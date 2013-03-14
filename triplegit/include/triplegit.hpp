/* TripleGit
(C) 2013 Niall Douglas http://www.nedprod.com/
File Created: Mar 2013
*/

#ifndef TRIPLEGIT_H
#define TRIPLEGIT_H

#include "../../NiallsCPP11Utilities/Int128_256.hpp"
#if !defined(_WIN32_WINNT) && defined(WIN32)
#define _WIN32_WINNT 0x0501
#endif
#define BOOST_THREAD_VERSION 3
#include "boost/graph/adjacency_list.hpp"
#include "boost/graph/adjacency_list_io.hpp"
#include "boost/thread/future.hpp"
#include <unordered_set>

/*! \file triplgit.hpp
\brief Declares TripleGit's main functionality
*/

#ifdef TRIPLEGIT_DLL_EXPORTS
#define TRIPLEGIT_API DLLEXPORTMARKUP
#else
#define TRIPLEGIT_API DLLIMPORTMARKUP
#endif

namespace std { namespace filesystem { class path; } }
namespace boost { namespace property_tree { template<typename Key, typename Data, typename KeyCompare = std::less<Key>> class basic_ptree; typedef basic_ptree< std::string, std::string > ptree; } }

namespace triplegit
{
	namespace async_io {
		//! For now, this is boost's future. Will be replaced when C++'s future catches up with boost's
		template<class T> class future : public boost::future<T>
		{
		public:
			future() { }
			future(boost::future<T> &&o) : boost::future<T>(std::move(o)) { }
		};
		//! For now, this is boost's future. Will be replaced when C++'s future catches up with boost's
		template<typename Iterator> Iterator wait_for_any(Iterator begin, Iterator end) { return boost::wait_for_any(begin, end); }
		//! For now, this is boost's future. Will be replaced when C++'s future catches up with boost's
		template<typename Iterator> void wait_for_all(Iterator begin, Iterator end) { boost::wait_for_all(begin, end); }
		//! For now, this is boost's future. Will be replaced when C++'s future catches up with boost's
		template<typename... Args> unsigned wait_for_any(Args... args) { return boost::wait_for_any(args...); }
		//! For now, this is boost's future. Will be replaced when C++'s future catches up with boost's
		template<typename... Args> void wait_for_all(Args... args) { boost::wait_for_all(args...); }
	} // namespace

namespace detail { void TRIPLEGIT_API prefetched_unique_id_source(void *ptr, size_t size); }

/*! \class unique_id
\brief A unique id
*/
template<class T, class hasher> class unique_id : public T
{
	static T int_init()
	{
		T ret;
		detail::prefetched_unique_id_source(&ret, sizeof(T));
		return ret;
	}
public:
	unique_id() : T(int_init()) { }
};

class fs_store;
/*! \class collection_id
\brief A collection in a fs_store
*/
class collection_id : public unique_id<NiallsCPP11Utilities::Int128, NiallsCPP11Utilities::Hash128>
{
public:
	//! Creates a new, unique, anonymous collection id
	collection_id(fs_store &store);
	//! Creates a named collection id
	collection_id(fs_store &store, const std::string &name, bool mustBeUnique=false);
};

/*! \class base_store
\brief Base class for a stored graph
*/
class TRIPLEGIT_API base_store
{
public:
	//! Returns the configuration of this graph store
	const boost::property_tree::ptree &config() const;
	//! Returns if this instance is read-only
	bool isReadOnly() const;
	//! Returns if this instance is read-only due to the filing system
	bool isFSReadOnly() const;

	//! Returns if a collection id is unique
	bool checkCollectionId(const collection_id &id) const;
	//! A set of collections
	typedef std::unordered_set<collection_id> collections_t;
};

/*! \class fs_store
\brief A graph stored in the filing system

If the filing system is read-only the store is automatically marked as read-only.
*/
class TRIPLEGIT_API fs_store : public base_store
{
public: 
	//! Constructs an instance using \em path
	fs_store(const std::filesystem::path &path, bool readOnly=false);
	fs_store(std::filesystem::path &&path, bool readOnly=false);
};

namespace detail {
	class TRIPLEGIT_API storable_vertices
	{
		bool amLoaded, amDirty;
		void *begin_batch_attachdetach();
		void do_batch_attach(void *p, base_store &store, collection_id id);
		void do_batch_detach(void *p, base_store &store, collection_id id);
		void end_batch_attachdetach(void *p);
	public:
		storable_vertices() : amLoaded(true), amDirty(true) { }

		//! Returns if loaded
		bool isLoaded() const { return amLoaded; }
		//! Returns if dirty
		bool isDirty() const { return amDirty; }

		//! Begins a commit to attached storage
		async_io::future<void> begincommit();
		//! Attachs this collection of vertices to the given store
		void attach(base_store &store, collection_id id)
		{
			void *p=begin_batch_attachdetach();
			auto unbatch=NiallsCPP11Utilities::Undoer([p, this]{end_batch_attachdetach(p);});
			do_batch_attach(p, store, id);
		}
		//! Attachs this collection of vertices to the given stores of std::pair<base_store &, collection_id>
		template<typename Iterator> void attach(Iterator begin, Iterator end)
		{
			void *p=begin_batch_attachdetach();
			auto unbatch=NiallsCPP11Utilities::Undoer([p, this]{end_batch_attachdetach(p);});
			for(; begin!=end; ++begin)
				do_batch_attach(p, begin->first, begin->second);
		}
		//! Detachs this collection of vertices from the given store
		void detach(base_store &store, collection_id id)
		{
			void *p=begin_batch_attachdetach();
			auto unbatch=NiallsCPP11Utilities::Undoer([p, this]{end_batch_attachdetach(p);});
			do_batch_detach(p, store, id);
		}
		//! Detachs this collection of vertices to the given stores of std::pair<base_store &, collection_id>
		template<typename Iterator> void detach(Iterator begin, Iterator end)
		{
			void *p=begin_batch_attachdetach();
			auto unbatch=NiallsCPP11Utilities::Undoer([p, this]{end_batch_attachdetach(p);});
			for(; begin!=end; ++begin)
				do_batch_detach(p, begin->first, begin->second);
		}
	};
}
/*! \class storable_vertices
\brief The base class for a storable collection of vertices
*/
template<class derived> class storable_vertices : public detail::storable_vertices
{
public:
};

namespace boost
{
	using namespace ::boost;
	/*! \class adjacency_list
	\brief A stored Boost.Graph adjacency_list
	*/
	template<class OutEdgeListS = ::boost::vecS, // a Sequence or an AssociativeContainer
            class VertexListS = ::boost::vecS, // a Sequence or a RandomAccessContainer
            class DirectedS = ::boost::directedS,
            class VertexProperty = ::boost::no_property,
            class EdgeProperty = ::boost::no_property,
            class GraphProperty = ::boost::no_property,
            class EdgeListS = ::boost::listS>
	class adjacency_list : public ::boost::adjacency_list<OutEdgeListS, VertexListS, DirectedS, VertexProperty, EdgeProperty, GraphProperty, EdgeListS>, public storable_vertices<adjacency_list<OutEdgeListS, VertexListS, DirectedS, VertexProperty, EdgeProperty, GraphProperty, EdgeListS>>
	{
		typedef ::boost::adjacency_list<OutEdgeListS, VertexListS, DirectedS, VertexProperty, EdgeProperty, GraphProperty, EdgeListS> Base;
	public:
		adjacency_list(base_store &store, collection_id id, const GraphProperty& p = GraphProperty()) : Base(p) { attach(store, id); }
		adjacency_list(const GraphProperty& p = GraphProperty()) : Base(p) { }
		adjacency_list(const adjacency_list& x) : Base(x) { }
		adjacency_list& operator=(const adjacency_list& x) {
			static_cast<Base &>(*this)=x;
			return *this;
		}
#if 0 // BGL doesn't support move construction yet
		adjacency_list(adjacency_list&& x) : Base(std::move(x)) { }
		adjacency_list& operator=(adjacency_list&& x) {
			static_cast<Base &>(*this)=std::move(x);
			return *this;
		}
#endif
		adjacency_list(vertices_size_type num_vertices, const GraphProperty& p = GraphProperty()) : Base(num_vertices, p) { }
		template <class EdgeIterator> adjacency_list(EdgeIterator first, EdgeIterator last,
                          vertices_size_type n,
                          edges_size_type = 0,
						  const GraphProperty& p = GraphProperty()) : Base(first, last, n, 0, p) { }
		template <class EdgeIterator, class EdgePropertyIterator> adjacency_list(EdgeIterator first, EdgeIterator last,
                          EdgePropertyIterator ep_iter,
                          vertices_size_type n,
                          edges_size_type = 0,
						  const GraphProperty& p = GraphProperty()) : Base(first, last, ep_iter, n, 0, p) { }
	};

} // namespace boost

} // namespace

#endif