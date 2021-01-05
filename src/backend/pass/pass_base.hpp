/*******************************************************************************
* Copyright 2020 Intel Corporation
*
* Licensed under the Apache License, Version 2.0 (the "License");
* you may not use this file except in compliance with the License.
* You may obtain a copy of the License at
*
*     http://www.apache.org/licenses/LICENSE-2.0
*
* Unless required by applicable law or agreed to in writing, software
* distributed under the License is distributed on an "AS IS" BASIS,
* WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
* See the License for the specific language governing permissions and
* limitations under the License.
*******************************************************************************/

#ifndef LLGA_BACKEND_PASS_PASS_BASE_HPP
#define LLGA_BACKEND_PASS_PASS_BASE_HPP

#include <functional>
#include <list>
#include <memory>
#include <queue>
#include <string>
#include <utility>
#include <vector>
#include <unordered_map>
#include <unordered_set>

#include "backend/pass/pass_backend.hpp"
#include "interface/graph.hpp"
#include "interface/ir.hpp"
#include "utils/json.hpp"

namespace dnnl {
namespace graph {
namespace impl {
namespace pass {

using graph = ::dnnl_graph_graph;
using namespace utils;
/*! \brief pass type */
enum class pass_type { kAnalysis = 0, kTransformation = 1 };

class pass_base;
class pattern;
using pass_base_ptr = std::shared_ptr<pass_base>;

// FCreatePattern: a function for defining pattern.
// One pass can have several FCreatePattern functions.
using FCreatePattern = std::function<void(pattern *apattern)>;

// FCreateOptPattern: a function for defining optimized pattern,
// which is used in the graph rewriting part to rewrite the pattern
// to optimized pattern.
// One pass can only have one FCreateOptPattern function.
using FCreateOptPattern = std::function<void(pattern *opt_pattern)>;

// FRequirement: a function to check if a graph node can meet the
// requirement of a pattern node
// One pattern node can have several FRequirement function.
using FRequirement = std::function<bool(node_t *graph_node)>;

class pattern {
private:
    /*! \brief nodes in this pattern */
    std::vector<std::shared_ptr<node_t>> nodes_;

public:
    /*!
     * \brief Create and add a node to this pattern.
     * \param aop_kind The operator used to create the node
     * \return node* created node
     */
    node_t *create_node(op_kind_t aop_kind) {
        nodes_.push_back(std::make_shared<node_t>(aop_kind));
        return nodes_.back().get();
    }

    /*!
     * \brief Get the starter node of this pattern.
     *        Any node in the vector of nodes_ can be the starter
     *        Any node except "any" in the vector of nodes_ can be the starter
     *        By default we use the first node in the vector
     * \return node* starter node
     */
    node_t *get_starter_node() {
        auto it = std::find_if(nodes_.begin(), nodes_.end(),
                [&](std::vector<std::shared_ptr<node_t>>::value_type &node)
                        -> bool {
                    return node->get_op_kind() != op_kind::any;
                });
        return it->get();
    }
};

/*!
 * \brief pass_base provides a base class for pass creation.
 *        A pass is used to do pattern matching on a given graph,
 *        and reconstruct a new graph based on optimized patterns.
 */
class pass_base {
public:
    pass_base(pass_type ptype, std::string pbackend, std::string pname);
    pass_base() {};

    virtual ~pass_base() = default;

    // the criteria of pass execution
    virtual void run(graph &agraph) {}
    // save pass basic information into json
    virtual void save(json::json_writer *writer) {
        writer->begin_object();
        writer->write_keyvalue("pass_name", name_);
        if (type_ == pass_type::kTransformation) {
            writer->write_keyvalue("pass_type", std::string("Transformation"));
        } else {
            writer->write_keyvalue("pass_type", std::string("Analysis"));
        }
        writer->write_keyvalue("pass_backend", backend_);
        writer->write_keyvalue("priority", priority_);
        writer->write_keyvalue("enable", enable_);
        writer->end_object();
    }
    // load pass basic information from json
    virtual void load(json::json_reader *reader) {
        json::read_helper helper;
        std::string type;
        helper.declare_field("pass_name", &name_);
        helper.declare_field("pass_type", &type);
        helper.declare_field("pass_backend", &backend_);
        helper.declare_field("priority", &priority_);
        helper.declare_field("enable", &enable_);
        helper.read_fields(reader);
    }

    pass_type get_pass_type() { return type_; }

    std::string get_pass_backend() { return backend_; }

    std::string get_pass_name() { return name_; }

    int get_pass_index() { return index_; }

    // set pass priority, passes with high priority
    // will be executed before passes with low priority
    pass_base &set_priority(float priority) {
        priority_ = priority;
        return *this;
    }
    float get_priority() { return priority_; }

    bool get_enable() { return enable_; }
    /*!
    * \brief Register additional attributes.
    * \param attr_name The name of the attribute.
    * \param value The value to be set.
    * \tparam value_type The type of the value to be set.
    */
    template <typename value_type>
    pass_base &set_attr(const std::string &attr_name, // NOLINT(*)
            const value_type &value) {
        attrs_.insert(make_pair(attr_name, value));
        return *this;
    }

    /*!
    * \brief Get additional registered attribute.
    * \param attr_name The name of the attribute.
    * \return An attribute of specified attr_name.
    * \tparam value_type The type of the attribute.
    */
    template <typename value_type>
    std::vector<value_type> get_attr(const std::string &attr_name) {
        std::vector<value_type> attr_vec;
        for (auto it = attrs_.begin(); it != attrs_.end(); ++it) {
            if (it->first == attr_name) { attr_vec.push_back(it->second); }
        }
        return attr_vec;
    }

    /*!
    * \brief check if pass has a specific attribute.
    * \param attr_name The name of the attribute.
    * \return bool: if this attribute exist in pass.
    */
    bool has_attr(const std::string &attr_name) {
        return attrs_.find(attr_name) != attrs_.end();
    }

protected:
    std::unordered_multimap<std::string, std::function<void(pattern *)>> attrs_;

private:
    pass_type type_;
    std::string backend_;
    std::string name_;
    int index_;
    float priority_ {5.0f};
    bool enable_ = true;
};

/*!
 * \brief analysis_pass provides analysis on a given graph,
 *        e.g. data type deduction, memory planning.
 */
class analysis_pass : public pass_base {
public:
    explicit analysis_pass(std::string pbackend, std::string pname)
        : pass_base(
                pass_type::kAnalysis, std::move(pbackend), std::move(pname)) {}
};

/*!
 * \brief transformation_pass generates an optimized graph
 *        when the pass is hit, it can be node replacements,
 *        dead branch elimination, etc.
 */
class transformation_pass : public pass_base {
public:
    explicit transformation_pass(std::string pbackend, std::string pname)
        : pass_base(pass_type::kTransformation, std::move(pbackend),
                std::move(pname)) {}

    static pass_base_ptr create(std::string pbackend, std::string pname) {
        return std::make_shared<transformation_pass>(
                std::move(pbackend), std::move(pname));
    }

    // the criteria of pass execution
    void run(graph &agraph) override;
};

/*!
 * \brief pass_registry is a registry class that
 *        is responsible for registering pass
 */
class pass_registry {
    using pass_create_fn = pass_base_ptr (*)(std::string, std::string);

public:
    // pass counter
    std::atomic<int> pass_counter {0};

    // get a static pass_backend_registry instance
    static pass_registry *get() {
        static pass_registry reg_inst;
        return &reg_inst;
    }

    // register a pass
    pass_base &register_pass(const std::string &backend_name,
            const std::string &pass_name, pass_create_fn fn);
    // get registered passes
    const std::list<pass_base_ptr> &get_passes() const { return passes_; }

    // sort passes based on their priorities, passes with high priority
    // will be executed before passes with low priority
    void sort_passes();

    pass_base_ptr &get_pass_ptr(const std::string &pass_name) {
        auto it = passes_map_.find(pass_name);
        assert(it != passes_map_.end() && "pass not exists!");
        return it->second;
    }

    pass_registry() = default;
    pass_registry(const pass_registry &) = delete;
    pass_registry(pass_registry &&) = delete;
    pass_registry &operator=(const pass_registry &) = delete;
    pass_registry &operator=(pass_registry &&) = delete;

private:
    std::list<pass_base_ptr> passes_;
    std::unordered_map<std::string, pass_base_ptr> passes_map_;
};

#define DECLARE_PASS_EX(bname, pname, counter) \
    static auto _registered_pass_##pname##_##bname##_##counter##_

#define DECLARE_PASS(bname, pname, counter) \
    DECLARE_PASS_EX(bname, pname, counter)

#define DNNL_GRAPH_REGISTER_TRANSFORMATION_PASS(backend_name, pass_class_name) \
    DECLARE_PASS(backend_name, pass_class_name, __COUNTER__) \
            = pass_registry::get()->register_pass(#backend_name, \
                    #pass_class_name, &transformation_pass::create)
} // namespace pass
} // namespace impl
} // namespace graph
} // namespace dnnl

#endif
