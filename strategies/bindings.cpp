#include <pybind11/pybind11.h>
#include <pybind11/stl.h>

#include "orderbook/flat_order_book.h"

namespace py = pybind11;
using namespace orderbook;

// Thin Python binding around the unmodified FlatOrderBook
PYBIND11_MODULE(orderbook_env, m) {
    py::enum_<Side>(m, "Side")
        .value("Buy", Side::Buy)
        .value("Sell", Side::Sell);

    py::class_<Trade>(m, "Trade")
        .def_readonly("taker_order_id", &Trade::taker_order_id)
        .def_readonly("maker_order_id", &Trade::maker_order_id)
        .def_readonly("price", &Trade::price)
        .def_readonly("quantity", &Trade::quantity);

    py::class_<FlatOrderBook>(m, "FlatOrderBook")
        .def(py::init<int64_t, size_t, size_t, uint64_t>(), py::arg("min_price"),
             py::arg("price_range"), py::arg("pool_capacity"), py::arg("max_order_id"))
        .def("add_order", &FlatOrderBook::addOrder, py::arg("order_id"), py::arg("side"),
             py::arg("price"), py::arg("quantity"), py::arg("timestamp_ns"))
        .def("cancel_order", &FlatOrderBook::cancelOrder, py::arg("order_id"))
        .def("best_bid", &FlatOrderBook::bestBid)
        .def("best_ask", &FlatOrderBook::bestAsk)
        .def("quantity_at", &FlatOrderBook::quantityAt, py::arg("side"), py::arg("price"));
}
