/*
 * File:   lrucache.hpp
 * Author: Alexander Ponomarev
 *
 * Created on June 20, 2013, 5:09 PM
 */


#ifndef _LRUCACHE_HPP_INCLUDED_
#define	_LRUCACHE_HPP_INCLUDED_

#include <mutex>
#include <shared_mutex>


#include <any>

#include <unordered_map>
#include <list>
#include <string>
#include <cstddef>
#include <stdexcept>

/*
 * Synchronized LRU cache
 */

using namespace std;

namespace cache {


    template<typename key_t, typename value_t>
    class lru_cache {

        mutable std::recursive_mutex m;

    public:
        typedef typename std::pair<key_t, value_t> key_value_pair_t;
        typedef typename std::list<key_value_pair_t>::iterator list_iterator_t;

        lru_cache(size_t max_size) :
                _max_size(max_size) {
        }




        void putIfDoesNotExist(const key_t& key, const value_t& value) {
            lock_guard<recursive_mutex> l(m);
            if (!exists(key))
                put(key, value);
        }

        void put(const key_t& key, const value_t& value) {

            lock_guard<recursive_mutex> l(m);

            auto it = _cache_items_map.find(key);
            _cache_items_list.push_front(key_value_pair_t(key, value));
            if (it != _cache_items_map.end()) {
                _cache_items_list.erase(it->second);
                _cache_items_map.erase(it);
            }
            _cache_items_map[key] = _cache_items_list.begin();

            if (_cache_items_map.size() > _max_size) {
                auto last = _cache_items_list.end();
                last--;
                _cache_items_map.erase(last->first);
                _cache_items_list.pop_back();
            }
        }




        const value_t& get(const key_t& key) {

            lock_guard<recursive_mutex> l(m);

            auto it = _cache_items_map.find(key);
            if (it == _cache_items_map.end()) {
                throw std::range_error("There is no such key in cache");
            } else {
                _cache_items_list.splice(_cache_items_list.begin(), _cache_items_list, it->second);
                return it->second->second;
            }
        }





        bool exists(const key_t& key)  {

            lock_guard<recursive_mutex> l(m);

            return _cache_items_map.find(key) != _cache_items_map.end();
        }

        size_t size() const {
            lock_guard<recursive_mutex> l(m);
            return _cache_items_map.size();
        }

    private:
        std::list<key_value_pair_t> _cache_items_list;
        std::unordered_map<key_t, list_iterator_t> _cache_items_map;
        size_t _max_size;
    };

} // namespace cache

#endif	/* _LRUCACHE_HPP_INCLUDED_ */
