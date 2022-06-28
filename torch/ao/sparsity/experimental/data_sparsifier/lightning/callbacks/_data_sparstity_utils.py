import logging
from torch.ao.sparsity.experimental.data_sparsifier.base_data_sparsifier import SUPPORTED_TYPES

logger: logging.Logger = logging.getLogger(__name__)


def _attach_model_to_data_sparsifier(module, data_sparsifier):
    for name, parameter in module.named_parameters():
        if type(parameter) in SUPPORTED_TYPES:
            valid_name = _get_valid_name(name)
            data_sparsifier.add_data(name=valid_name, data=parameter)  # will be defaulted to default configs


def _get_valid_name(name):
    return name.replace('.', '_')  # . is not allowed as a name


def _log_sparsified_level(model, data_sparsifier) -> None:
    # Show the level of sparsity AFTER step:
    for name, parameter in model.named_parameters():
        if not (type(parameter) in SUPPORTED_TYPES):
            continue
        valid_name = _get_valid_name(name)
        mask = data_sparsifier.get_mask(name=valid_name)
        sparsity_level = 1.0 - mask.float().mean()
        logger.info(
            f"Sparsity in layer {name} = {sparsity_level: .2%}"
        )